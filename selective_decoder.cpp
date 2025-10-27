#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdexcept>
#include <cstring>
#include <lz4.h>
#include "datastruct.hpp"

class MMapDecoderSelective {
    uint8_t* fileData = nullptr;
    size_t fileSize = 0;
    size_t masterOffset = 0;
    size_t baseOffset = 0;
    std::vector<std::string> query;
    long queryOffset = 0;

    std::vector<std::string> dictionary;
    std::vector<long> entityTable;
    std::unordered_map<uint8_t, size_t> customSizeMap;

public:
    ~MMapDecoderSelective() {
        if (fileData) munmap(fileData, fileSize);
    }

    void setQuery(std::vector<std::string>& q){
        queryOffset = 0;
        query = q;
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

    const uint8_t* readNBytesPtr(size_t n) {
        if (masterOffset + n > fileSize) {
            throw std::runtime_error("EOF: Attempted to read past end of file.");
        }
        const uint8_t* ptr = fileData + masterOffset;
        masterOffset += n;
        return ptr;
    }

    uint64_t readVarNumber() {
        uint8_t size_byte = readByte();
        if (size_byte < 128) return static_cast<uint64_t>(size_byte);

        size_t len = size_byte & 0x7F;
        const uint8_t* arr = readNBytesPtr(len);
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

    uint8_t readByte() {
        if (masterOffset >= fileSize) throw std::runtime_error("EOF: Attempted to read a single byte past end of file.");
        return fileData[masterOffset++];
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

    Value decodeValue() {
        uint8_t byte = readByte();

        if ((byte & 0x80) == 0) {
            size_t strSize = byte & 0x7F;
            if (strSize == 0x7F) {
                size_t compressedSize = readVarNumber();
                size_t originalSize = readVarNumber();
                const uint8_t* comp_ptr = readNBytesPtr(compressedSize);
                auto decomp = uncompressBuffer(comp_ptr, compressedSize, originalSize);
                return Value(std::string(decomp.begin(), decomp.end()));
            } else {
                const uint8_t* str_ptr = readNBytesPtr(strSize);
                return Value(std::string(reinterpret_cast<const char*>(str_ptr), strSize));
            }
        }

        if (((byte & 0xE0) >> 5) == 0x04 || ((byte & 0xE0) >> 5) == 0x05) {
            uint64_t id = byte & 0x1F;
            if (id == 0x1F) id = readVarNumber();
            return decodeWrapper(id);
        }

        switch (byte & 0xF0) {
            case 0xC0: return Value(int64_t(byte & 0x0F));
            case 0xD0: return Value(-int64_t(byte & 0x0F));
            case 0xE0: {
                uint64_t id = byte & 0x0F;
                if (id == 0x0F) id = readVarNumber();
                size_t sz = customSizeMap.at(id);
                const uint8_t* d = readNBytesPtr(sz);
                return Custom(id, std::vector<uint8_t>(d, d + sz)).toValue();
            }
            case 0xF0: {
                uint8_t subType = byte & 0x0F;
                switch (subType) {
                    case 0x0C: return Value();
                    case 0x0D: return Value(readByte());
                    case 0x0E: return Value(false);
                    case 0x0F: return Value(true);
                }

                if (subType <= 0x07) {
                    size_t len = 1 << (subType & 0x03);
                    const uint8_t* data = readNBytesPtr(len);
                    int64_t val = 0;
                    std::memcpy(&val, data, len);
                    if (subType & 0x04) val = -val;
                    return Value(val);
                }

                if (subType == 0x08) {
                    const uint8_t* data = readNBytesPtr(4);
                    float fval;
                    std::memcpy(&fval, data, sizeof(float));
                    return Value(fval);
                }

                if (subType == 0x09) {
                    const uint8_t* data = readNBytesPtr(8);
                    double dval;
                    std::memcpy(&dval, data, sizeof(double));
                    return Value(dval);
                }
                throw std::runtime_error("Unhandled F0 subtype");
            }
        }
        throw std::runtime_error("Unknown type byte");
    }

    Value decodeObjectSelective() {
        uint8_t byte = readByte();
        long count = byte & 0x7F;
        if (count == 0x7F) count = readVarNumber();

        Object obj;
        long offsetSize = readByte();

        int low = 0;
        int high = count - 1;

        std::string target = query[queryOffset++];

        long savedOffset = masterOffset;

        long baseOffsetForData = masterOffset + (count * offsetSize);

        while (low <= high) {
            int mid = low + (high - low) / 2;

            masterOffset = (mid * offsetSize) + savedOffset; 
            if (masterOffset >= fileSize) {
                throw std::runtime_error("EOF: Attempted to read past end of file.");
            }

            const uint8_t* keyOffsetPtr = readNBytesPtr(offsetSize);
            long keyOffset = 0;
            std::memcpy(&keyOffset, keyOffsetPtr, offsetSize);

            masterOffset = keyOffset + baseOffsetForData;

            if (masterOffset >= fileSize) {
                throw std::runtime_error("EOF: Attempted to read past end of file.");
            }

            long keyIdx = readVarNumber();
            if (keyIdx >= dictionary.size()) throw std::runtime_error("Invalid key index");
            auto key = dictionary[keyIdx];

            if (key == target) {
                return decodeValue();
            }
            if (key < target) {
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }

        throw std::runtime_error("The Key is not valid");
    }

    Value decodeListSelective() {

        uint8_t byte = readByte();
        long count = byte & 0x7F;
        if (count == 0x7F) count = readVarNumber();

        List l;
        long offsetSize = readByte();

        int low = 0;
        int high = count - 1;

        long baseOffsetForData = masterOffset + (count * offsetSize);

        std::string targetString = query[queryOffset++];

        long target = std::stol(targetString);

        masterOffset += target * offsetSize;

        if (masterOffset >= fileSize) {
            throw std::runtime_error("EOF: Attempted to read past end of file.");
        }

        const uint8_t* valueOffsetPtr = readNBytesPtr(offsetSize);
        long valueOffset = 0;
        std::memcpy(&valueOffset, valueOffsetPtr, offsetSize);

        masterOffset = valueOffset + baseOffsetForData;

        if (masterOffset >= fileSize) {
            throw std::runtime_error("EOF: Attempted to read past end of file.");
        }

        return decodeValue();
    }

        Value decodeObject() {
        uint8_t byte = readByte();
        long count = byte & 0x7F;
        if (count == 0x7F) count = readVarNumber();

        Object obj;
        long offsetSize = readByte();
        
        masterOffset += offsetSize * count;

        for (int i = 0; i < count; i++) {
            long keyIdx = readVarNumber();
            if (keyIdx >= dictionary.size()) throw std::runtime_error("Invalid key index");
            obj.add(dictionary[keyIdx], decodeValue());
        }
        return obj.toValue();
    }

    Value decodeList() {
        uint8_t byte = readByte();
        long count = byte & 0x7F;
        if (count == 0x7F) count = readVarNumber();

        List l;
        long offsetSize = readByte();

        masterOffset += offsetSize * count;
        
        l.elements.reserve(count);
        for (int i = 0; i < count; i++) {
            l.add(decodeValue());
        }
        return l.toValue();
    }


    Value decodeWrapper(long id) {
        size_t savedOffset = masterOffset;
        masterOffset = entityTable.at(id) + baseOffset;
        
        uint8_t peek = fileData[masterOffset];
        Value v;
        
        if(queryOffset < query.size()){
            v = (peek & 0x80) ? decodeListSelective() : decodeObjectSelective();
        }
        else v = (peek & 0x80) ? decodeList() : decodeObject();
        
        masterOffset = savedOffset;
        return v;
    }

    Value decode(const std::string& filename) {
        loadFile(filename);
        
        long headerLength = readVarNumber();
        long entityCount = readVarNumber();

        uint8_t dictFlag = readByte();
        std::vector<uint8_t> dictBuffer;
        if (dictFlag == 0xFF) {
            long sz = readVarNumber();
            long og = readVarNumber();
            const uint8_t* comp_ptr = readNBytesPtr(sz);
            dictBuffer = uncompressBuffer(comp_ptr, sz, og);
        } else {
            const uint8_t* dict_ptr = readNBytesPtr(dictFlag);
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

        uint8_t offsetSize = readByte();
        entityTable.reserve(entityCount);
        for (long i = 0; i < entityCount; i++) {
            const uint8_t* b_ptr = readNBytesPtr(offsetSize);
            long val = 0;
            std::memcpy(&val, b_ptr, offsetSize);
            entityTable.push_back(val);
        }
        
        baseOffset = masterOffset;
        return decodeWrapper(0);
    }
};