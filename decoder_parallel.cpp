#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <lz4.h>
#include <thread>
#include <mutex>
#include <functional>
#include "datastruct.hpp"

class MMapDecoderParallel {
    uint8_t* fileData = nullptr;
    size_t fileSize = 0;
    size_t baseOffset = 0;

    std::vector<std::string> dictionary;
    std::vector<long> entityTable;
    std::unordered_map<uint8_t, size_t> customSizeMap;
    std::unordered_map<long, Value> entityMap;
    std::unordered_map<long, size_t> offsetMap;

    long nextEntityId = 0;
    std::mutex nextEntityIdMutex;
    std::mutex entityMapMutex;

public:
    ~MMapDecoderParallel() {
        if (fileData) munmap(fileData, fileSize);
    }

    void loadFile(const std::string& filename) {
        int fd = open(filename.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("Cannot open file");

        struct stat st;
        if (fstat(fd, &st) < 0) {
            close(fd);
            throw std::runtime_error("Cannot get file stats");
        }
        fileSize = st.st_size;

        if (fileSize > 0) {
            fileData = (uint8_t*)mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
            if (fileData == MAP_FAILED) {
                close(fd);
                throw std::runtime_error("mmap failed");
            }
        }
        close(fd);
    }

    void addCustom(uint8_t id, size_t size) {
        customSizeMap[id] = size;
    }

    const uint8_t* readNBytesPtr(size_t n, long threadID) {
        if (offsetMap.at(threadID) + n > fileSize) {
            throw std::runtime_error("EOF: Attempted to read past end of file.");
        }
        const uint8_t* ptr = fileData + offsetMap.at(threadID);
        offsetMap.at(threadID) += n;
        return ptr;
    }

    uint8_t readByte(long threadID) {
        if (offsetMap.at(threadID) >= fileSize) throw std::runtime_error("EOF: Attempted to read a single byte past end of file.");
        return fileData[offsetMap.at(threadID)++];
    }

    uint64_t readVarNumber(long threadID) {
        uint8_t size_byte = readByte(threadID);
        if (size_byte < 128) return static_cast<uint64_t>(size_byte);

        size_t len = size_byte & 0x7F;
        const uint8_t* arr = readNBytesPtr(len, threadID);
        uint64_t result = 0;
        if (len > sizeof(uint64_t)) len = sizeof(uint64_t);
        std::memcpy(&result, arr, len);
        return result;
    }

    std::pair<uint64_t, size_t> readVarNumberFromBuffer(const std::vector<uint8_t>& buffer, size_t offset) {
        if (offset >= buffer.size()) throw std::runtime_error("Buffer underflow at start.");
        
        uint8_t sizeByte = buffer[offset];
        if (sizeByte < 128) return { sizeByte, 1 };

        size_t len = sizeByte & 0x7F;
        if (offset + 1 + len > buffer.size()) throw std::runtime_error("Buffer underflow for multi-byte number.");
        
        uint64_t result = 0;
        if (len > sizeof(uint64_t)) len = sizeof(uint64_t);
        std::memcpy(&result, buffer.data() + offset + 1, len);
        return { result, 1 + len };
    }

    std::vector<uint8_t> uncompressBuffer(const uint8_t* compressed_ptr, size_t compressed_size, size_t originalSize) {
        std::vector<uint8_t> output(originalSize);
        int decompressed = LZ4_decompress_safe(
            reinterpret_cast<const char*>(compressed_ptr),
            reinterpret_cast<char*>(output.data()),
            static_cast<int>(compressed_size),
            static_cast<int>(originalSize)
        );
        if (decompressed < 0) throw std::runtime_error("LZ4 decompression failed");
        output.resize(decompressed);
        return output;
    }

    Value decodeValue(long threadID) {
        uint8_t byte = readByte(threadID);

        if ((byte & 0x80) == 0) {
            size_t strSize = byte & 0x7F;
            if (strSize == 0x7F) {
                size_t compressedSize = readVarNumber(threadID);
                size_t originalSize = readVarNumber(threadID);
                const uint8_t* comp_ptr = readNBytesPtr(compressedSize, threadID);
                auto decomp = uncompressBuffer(comp_ptr, compressedSize, originalSize);
                return Value(std::string(decomp.begin(), decomp.end()));
            } else {
                const uint8_t* str_ptr = readNBytesPtr(strSize, threadID);
                return Value(std::string(reinterpret_cast<const char*>(str_ptr), strSize));
            }
        }

        if (((byte & 0xE0) >> 5) == 0x04 || ((byte & 0xE0) >> 5) == 0x05) {
            uint64_t id = byte & 0x1F;
            if (id == 0x1F) id = readVarNumber(threadID);
            return Reference(id).toValue();
        }

        switch (byte & 0xF0) {
            case 0xC0: return Value(int64_t(byte & 0x0F));
            case 0xD0: return Value(-int64_t(byte & 0x0F));
            case 0xE0: {
                uint64_t id = byte & 0x0F;
                if (id == 0x0F) id = readVarNumber(threadID);
                size_t sz = customSizeMap.at(id);
                const uint8_t* d = readNBytesPtr(sz, threadID);
                return Custom(id, std::vector<uint8_t>(d, d + sz)).toValue();
            }
            case 0xF0: {
                uint8_t subType = byte & 0x0F;
                switch (subType) {
                    case 0x0C: return Value();
                    case 0x0D: return Value(readByte(threadID));
                    case 0x0E: return Value(false);
                    case 0x0F: return Value(true);
                }

                if (subType <= 0x07) {
                    size_t len = 1 << (subType & 0x03);
                    const uint8_t* data = readNBytesPtr(len, threadID);
                    int64_t val = 0;
                    std::memcpy(&val, data, len);
                    if (subType & 0x04) val = -val;
                    return Value(val);
                }

                if (subType == 0x08) {
                    const uint8_t* data = readNBytesPtr(4, threadID);
                    float fval;
                    std::memcpy(&fval, data, sizeof(float));
                    return Value(fval);
                }

                if (subType == 0x09) {
                    const uint8_t* data = readNBytesPtr(8, threadID);
                    double dval;
                    std::memcpy(&dval, data, sizeof(double));
                    return Value(dval);
                }
                throw std::runtime_error("Unhandled F0 subtype");
            }
        }
        throw std::runtime_error("Unknown type byte");
    }

    Value decodeObject(long threadID) {
        uint8_t byte = readByte(threadID);
        long count = byte & 0x7F;
        if (count == 0x7F) count = readVarNumber(threadID);

        Object obj;
        long offsetSize = readByte(threadID);
        
        offsetMap[threadID] += offsetSize * count;

        for (int i = 0; i < count; i++) {
            long keyIdx = readVarNumber(threadID);
            if (keyIdx >= dictionary.size()) throw std::runtime_error("Invalid key index");
            obj.add(dictionary[keyIdx], decodeValue(threadID));
        }
        return obj.toValue();
    }

    Value decodeList(long threadID) {
        uint8_t byte = readByte(threadID);
        long count = byte & 0x7F;
        if (count == 0x7F) count = readVarNumber(threadID);

        List l;
        long offsetSize = readByte(threadID);
        
        offsetMap[threadID] += offsetSize * count;
        
        l.elements.reserve(count);
        for (int i = 0; i < count; i++) {
            l.add(decodeValue(threadID));
        }
        return l.toValue();
    }

    Value decodeWrapper(long id, long threadID) {
        offsetMap[threadID] = entityTable.at(id) + baseOffset;
        uint8_t peek = fileData[offsetMap.at(threadID)];
        Value v = (peek & 0x80) ? decodeList(threadID) : decodeObject(threadID);
        return v;
    }

    void resolveReferences(Value& value, std::unordered_set<long>& visited) {
        if (!value.isReference()) {
            if (value.isObject()) {
                for (auto& pair : value.asObject().fields) {
                    resolveReferences(pair.second, visited);
                }
            } else if (value.isList()) {
                for (auto& element : value.asList().elements) {
                    resolveReferences(element, visited);
                }
            }
            return;
        }

        long id = value.asReference().id;

        if (visited.count(id)) {
            value = Value(); 
            return;
        }

        visited.insert(id);

        auto it = entityMap.find(id);
        if (it != entityMap.end()) {
            value = it->second;
            resolveReferences(value, visited);
        } else {
            value = Value(); 
        }

        visited.erase(id);
    }

    Value decode(const std::string& filename) {
        loadFile(filename);
            
        offsetMap[0] = 0;

        long headerLength = readVarNumber(0);
        long entityCount = readVarNumber(0);

        uint8_t dictFlag = readByte(0);
        std::vector<uint8_t> dictBuffer;
        if (dictFlag == 0xFF) {
            long sz = readVarNumber(0);
            long og = readVarNumber(0);
            const uint8_t* comp_ptr = readNBytesPtr(sz, 0);
            dictBuffer = uncompressBuffer(comp_ptr, sz, og);
        } else {
            const uint8_t* dict_ptr = readNBytesPtr(dictFlag, 0);
            dictBuffer.assign(dict_ptr, dict_ptr + dictFlag);
        }

        size_t dictOffset = 0;
        while (dictOffset < dictBuffer.size()) {
            auto [stringLength, bytesConsumed] = readVarNumberFromBuffer(dictBuffer, dictOffset);
            dictOffset += bytesConsumed;
            if (dictOffset + stringLength > dictBuffer.size()) {
                throw std::runtime_error("Invalid dictionary format");
            }
            dictionary.emplace_back(reinterpret_cast<const char*>(dictBuffer.data() + dictOffset), stringLength);
            dictOffset += stringLength;
        }

        uint8_t offsetSize = readByte(0);
        entityTable.reserve(entityCount);
        for (long i = 0; i < entityCount; i++) {
            const uint8_t* b_ptr = readNBytesPtr(offsetSize, 0);
            long val = 0;
            std::memcpy(&val, b_ptr, offsetSize);
            entityTable.push_back(val);
        }
        
        baseOffset = offsetMap[0];
        nextEntityId = 0;

        long threadCount = 4;
        std::vector<std::thread> threads;

        for (long i = 0; i < threadCount; ++i) {
            offsetMap[i] = 0; 
        }

        auto worker_task = [&](long threadID) {
            while (true) {
                long currentId = -1;
                {
                    std::lock_guard<std::mutex> lock(nextEntityIdMutex);
                    if (nextEntityId < entityCount) {
                        currentId = nextEntityId++;
                    } else {
                        break;
                    }
                }

                if (currentId != -1) {
                    Value v = decodeWrapper(currentId, threadID);
                    {
                        std::lock_guard<std::mutex> lock(entityMapMutex);
                        entityMap[currentId] = v;
                    }
                }
            }
        };

        for (long i = 0; i < threadCount; ++i) {
            threads.emplace_back(worker_task, i);
        }

        for (auto& t : threads) {
            t.join();
        }

        if (entityMap.find(0) == entityMap.end()) {
             throw std::runtime_error("Root entity (ID 0) not found after decoding.");
        }
        
        Value root = entityMap.at(0);
        std::unordered_set<long> visited;
        resolveReferences(root, visited);

        return root;
    }
};