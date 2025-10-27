#include "encoder_parallel.hpp"
#include <future>
#include <mutex>
#include <atomic>
#include <vector>
#include <map>
#include <queue>
#include <functional>
#include <thread>

EncoderP::EncoderP() : pool_stop(false) {
    init_pool();
}

EncoderP::~EncoderP() {
    stop_pool();
}

void EncoderP::init_pool() {
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    for(unsigned int i = 0; i < num_threads; ++i) {
        pool_workers.emplace_back([this] {
            while(true) {
                std::packaged_task<std::pair<long, std::vector<uint8_t>> ()> task;
                {
                    std::unique_lock<std::mutex> lock(this->pool_mutex);
                    this->pool_cv.wait(lock, [this]{ return this->pool_stop || !this->pool_tasks.empty(); });
                    if(this->pool_stop && this->pool_tasks.empty()) return;
                    task = std::move(this->pool_tasks.front());
                    this->pool_tasks.pop();
                }
                task();
            }
        });
    }
}

void EncoderP::stop_pool() {
    {
        std::unique_lock<std::mutex> lock(pool_mutex);
        pool_stop = true;
    }
    pool_cv.notify_all();
    for(std::thread &worker : pool_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::future<std::pair<long, std::vector<uint8_t>>> EncoderP::enqueue_task(std::function<std::pair<long, std::vector<uint8_t>>()> task_func) {
    std::packaged_task<std::pair<long, std::vector<uint8_t>> ()> task(std::move(task_func));
    std::future<std::pair<long, std::vector<uint8_t>>> res = task.get_future();
    {
        std::unique_lock<std::mutex> lock(pool_mutex);
        if(pool_stop) throw std::runtime_error("enqueue on stopped ThreadPool");
        pool_tasks.emplace(std::move(task));
    }
    pool_cv.notify_one();
    return res;
}

void EncoderP::serial_build_key(const std::string& key) {
    std::lock_guard<std::mutex> lock(dictionary_mutex);
    auto it = dictionary_map.find(key);
    if (it == dictionary_map.end()) {
        uint64_t index = dictionary_list.size();
        dictionary_list.push_back(key);
        dictionary_map[key] = index;
    }
}

std::vector<uint8_t> EncoderP::get_key_encoding(const std::string& key) {
    auto it = dictionary_map.find(key);
    if (it == dictionary_map.end()) {
         throw std::runtime_error("Key not found in dictionary: " + key);
    }
    return varEncodeNumber(it->second);
}

std::vector<uint8_t> EncoderP::parallel_encode_list(const List& entity, const std::map<const Value*, long>& id_map) {
    std::vector<uint8_t> output;
    std::vector<long> offsetTableLong;
    std::vector<uint8_t> dataValue;

    for (const auto& value : entity.elements) {
        offsetTableLong.push_back(dataValue.size());
        if (value.type() == ValueType::List || value.type() == ValueType::Object) {
            long childId = id_map.at(&value);
            auto referenceCode = generateReferenceCode(value.type(), childId);
            dataValue.insert(dataValue.end(), referenceCode.begin(), referenceCode.end());
        } else {
            encodePrimitive(value, dataValue);
        }
    }

    size_t length = entity.elements.size();
    if (length < 127) {
        output.push_back(0x80 | (length & 0x7F));
    } else {
        output.push_back(0xFF);
        auto varEncodedListLength = varEncodeNumber(length);
        output.insert(output.end(), varEncodedListLength.begin(), varEncodedListLength.end());
    }

    int offsetByteCount = nearestBytes(dataValue.size());
    output.push_back(static_cast<uint8_t>(offsetByteCount));

    for (long offset : offsetTableLong) {
        auto offsetEncoded = fixedEncodeNumber(offset, offsetByteCount * 8);
        output.insert(output.end(), offsetEncoded.begin(), offsetEncoded.end());
    }
    output.insert(output.end(), dataValue.begin(), dataValue.end());
    return output;
}

std::vector<uint8_t> EncoderP::parallel_encode_object(const Object& entity, const std::map<const Value*, long>& id_map) {
    std::vector<uint8_t> output;
    std::vector<long> offsetTableLong;
    std::vector<uint8_t> dataValue;

    for (const auto& kvPair : entity.fields) {
        offsetTableLong.push_back(dataValue.size());
        
        auto encodedKey = get_key_encoding(kvPair.first); 
        dataValue.insert(dataValue.end(), encodedKey.begin(), encodedKey.end());

        const auto& value = kvPair.second;
        if (value.type() == ValueType::List || value.type() == ValueType::Object) {
            long childId = id_map.at(&value); 
            auto referenceCode = generateReferenceCode(value.type(), childId);
            dataValue.insert(dataValue.end(), referenceCode.begin(), referenceCode.end());
        } else {
            encodePrimitive(value, dataValue);
        }
    }

    size_t length = entity.fields.size();
    if (length < 127) {
        output.push_back(length & 0x7F);
    } else {
        output.push_back(0x7F);
        auto varEncodedObjLength = varEncodeNumber(length);
        output.insert(output.end(), varEncodedObjLength.begin(), varEncodedObjLength.end());
    }

    int offsetByteCount = nearestBytes(dataValue.size());
    output.push_back(static_cast<uint8_t>(offsetByteCount));

    for (long offset : offsetTableLong) {
        auto offsetEncoded = fixedEncodeNumber(offset, offsetByteCount * 8);
        output.insert(output.end(), offsetEncoded.begin(), offsetEncoded.end());
    }
    output.insert(output.end(), dataValue.begin(), dataValue.end());
    return output;
}

std::pair<long, std::vector<uint8_t>> EncoderP::parallel_encode_value(const Value* value, long id, const std::map<const Value*, long>* id_map) {
    std::vector<uint8_t> data;
    if (value->type() == ValueType::Object) {
        data = parallel_encode_object(std::get<Object>(value->data), *id_map);
    } else if (value->type() == ValueType::List) {
        data = parallel_encode_list(std::get<List>(value->data), *id_map);
    }
    return {id, std::move(data)};
}

void EncoderP::encode(const Value& root, const std::string& filename) {

    dictionary_list.clear();
    dictionary_map.clear();
    entityOffsetTable.clear();

    std::map<const Value*, long> id_map;
    std::vector<const Value*> jobs;
    
    std::vector<const Value*> stack;
    stack.push_back(&root);
    
    currentEntityId = 0;

    while(!stack.empty()){
        const Value* value = stack.back();
        stack.pop_back();

        if (id_map.count(value)) {
            continue;
        }

        long id = currentEntityId.fetch_add(1);
        id_map[value] = id;
        jobs.push_back(value);

        if (value->type() == ValueType::Object) {
            const auto& obj = std::get<Object>(value->data);

            for (int i = obj.fields.size() - 1; i >= 0; --i) {
                serial_build_key(obj.fields[i].first); 
                const auto& childVal = obj.fields[i].second;
                if (childVal.type() == ValueType::Object || childVal.type() == ValueType::List) {
                    stack.push_back(&childVal);
                }
            }
        } else if (value->type() == ValueType::List) {
            const auto& list = std::get<List>(value->data);
            
            for (int i = list.elements.size() - 1; i >= 0; --i) {
                const auto& childVal = list.elements[i];
                if (childVal.type() == ValueType::Object || childVal.type() == ValueType::List) {
                    stack.push_back(&childVal);
                }
            }
        }
    }

    long totalEntities = currentEntityId; 

    std::vector<std::future<std::pair<long, std::vector<uint8_t>>>> tasks;
    for (long id = 0; id < totalEntities; ++id) {
        const Value* v = jobs[id];
        tasks.push_back(enqueue_task([this, v, id, &id_map]() {
            return this->parallel_encode_value(v, id, &id_map);
        }));
    }

    std::map<long, std::vector<uint8_t>> results;
    for(auto& f : tasks) {
        auto pair = f.get();
        results[pair.first] = std::move(pair.second);
    }

    std::vector<uint8_t> output;
    output.reserve(1024 * 1024);

    for (long i = 0; i < totalEntities; ++i) {
        entityOffsetTable[i] = output.size();
        auto& chunk = results.at(i);
        output.insert(output.end(), std::make_move_iterator(chunk.begin()), std::make_move_iterator(chunk.end()));
    }

    std::vector<uint8_t> header;
    header.reserve(4096);

    auto varEncodedEntityCount = varEncodeNumber(totalEntities);
    header.insert(header.end(), varEncodedEntityCount.begin(), varEncodedEntityCount.end());

    std::vector<uint8_t> dictionaryBuffer;
    for (const auto& str : dictionary_list) {
        auto stringSize = varEncodeNumber(str.size());
        dictionaryBuffer.insert(dictionaryBuffer.end(), stringSize.begin(), stringSize.end());
        dictionaryBuffer.insert(dictionaryBuffer.end(), str.begin(), str.end());
    }

    if (dictionaryBuffer.size() < 255) {
        header.push_back((uint8_t)dictionaryBuffer.size());
        header.insert(header.end(), dictionaryBuffer.begin(), dictionaryBuffer.end());
    } else {
        auto ogSizeVec = varEncodeNumber(dictionaryBuffer.size());
        auto compressedDict = compressBuffer(dictionaryBuffer);
        auto compressedSizeVec = varEncodeNumber(compressedDict.size());

        header.push_back(0xFF);
        header.insert(header.end(), compressedSizeVec.begin(), compressedSizeVec.end());
        header.insert(header.end(), ogSizeVec.begin(), ogSizeVec.end());
        header.insert(header.end(), compressedDict.begin(), compressedDict.end());
    }

    auto globalOffsetBytes = nearestBytes(output.size());
    header.push_back(static_cast<uint8_t>(globalOffsetBytes));

    for (uint32_t eid = 0; eid < totalEntities; ++eid) {
        auto offsetLong = entityOffsetTable[eid];
        auto offsetBinary = fixedEncodeNumber(offsetLong, globalOffsetBytes * 8);
        header.insert(header.end(), offsetBinary.begin(), offsetBinary.end());
    }

    auto headerSizeVarEncoded = varEncodeNumber(header.size());
    
    std::ofstream fout(filename, std::ios::binary);
    fout.write(reinterpret_cast<const char*>(headerSizeVarEncoded.data()), headerSizeVarEncoded.size());
    fout.write(reinterpret_cast<const char*>(header.data()), header.size());
    fout.write(reinterpret_cast<const char*>(output.data()), output.size());
}

std::vector<uint8_t> EncoderP::encodeKey(const std::string& key){
    std::lock_guard<std::mutex> lock(dictionary_mutex);
    auto it = dictionary_map.find(key);
    if (it != dictionary_map.end()) {
        return varEncodeNumber(it->second);
    } else {
        uint64_t index = dictionary_list.size();
        dictionary_list.push_back(key);
        dictionary_map[key] = index;
        return varEncodeNumber(index);
    }
}

void EncoderP::encodePrimitive(const Value& value, std::vector<uint8_t>& out) {
    switch (value.type()) {
        case ValueType::Boolean: {
            out.push_back(std::get<bool>(value.data) ? 0xFF : 0xFE);
            break;
        }
        case ValueType::Null: {
            out.push_back(0xFC);
            break;
        }
        case ValueType::Byte: {
            out.push_back(0xFD);
            out.push_back(std::get<uint8_t>(value.data));
            break;
        }
        case ValueType::Integer: {
            int64_t n = std::get<int64_t>(value.data);
            uint8_t meta = (n >= 0) ? ((n < 16) ? 0xC0 : 0xF0) : ((n > -16) ? 0xD0 : 0xF4);
            int64_t abs_n = (n >= 0) ? n : -n;
            if (abs_n < 16) {
                out.push_back(meta | (abs_n & 0x0F));
            } else {
                out.push_back(meta);
                if (abs_n <= UINT8_MAX) {
                    out.push_back(static_cast<uint8_t>(abs_n));
                } else if (abs_n <= UINT16_MAX) {
                    out.back() |= 0x01;
                    auto encodedInt = fixedEncodeNumber(abs_n, 16);
                    out.insert(out.end(), encodedInt.begin(), encodedInt.end());
                } else if (abs_n <= UINT32_MAX) {
                    out.back() |= 0x02;
                    auto encodedInt = fixedEncodeNumber(abs_n, 32);
                    out.insert(out.end(), encodedInt.begin(), encodedInt.end());
                } else {
                    out.back() |= 0x03;
                    auto encodedInt = fixedEncodeNumber(abs_n, 64);
                    out.insert(out.end(), encodedInt.begin(), encodedInt.end());
                }
            }
            break;
        }
        case ValueType::String: {
            const std::string& strData = std::get<std::string>(value.data);
            if (strData.length() < 127) {
                out.push_back(strData.length() & 0x7F);
                out.insert(out.end(), strData.begin(), strData.end());
            } else {
                out.push_back(0x7F);
                std::vector<uint8_t> bytes(strData.begin(), strData.end());
                auto ogSize = varEncodeNumber(bytes.size());
                bytes = compressBuffer(bytes);
                auto compressedSize = varEncodeNumber(bytes.size());
                out.insert(out.end(), compressedSize.begin(), compressedSize.end());
                out.insert(out.end(), ogSize.begin(), ogSize.end());
                out.insert(out.end(), bytes.begin(), bytes.end());
            }
            break;
        }
        case ValueType::Float: {
            double f = std::get<double>(value.data);
            if (f >= -FLT_MAX && f <= FLT_MAX) {
                out.push_back(0xF8);
                float f32 = static_cast<float>(f);
                uint32_t bits;
                std::memcpy(&bits, &f32, sizeof(float));
                for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
            } else {
                out.push_back(0xF9);
                uint64_t bits;
                std::memcpy(&bits, &f, sizeof(double));
                for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
            }
            break;
        }
        case ValueType::Custom: {
            const auto& custom_obj = std::get<Custom>(value.data);
            uint8_t meta = (custom_obj.id < 15) ? (0xE0 | custom_obj.id) : 0xEF;
            out.push_back(meta);
            if (custom_obj.id >= 15) {
                auto idVarEncoded = varEncodeNumber(custom_obj.id);
                out.insert(out.end(), idVarEncoded.begin(), idVarEncoded.end());
            }
            out.insert(out.end(), custom_obj.data.begin(), custom_obj.data.end());
            break;
        }
        default:
            throw std::runtime_error("Unsupported primitive type");
    }
}

std::vector<uint8_t> EncoderP::varEncodeNumber(uint64_t number) {
    std::vector<uint8_t> encoded;
    if (number < 128) {
        encoded.push_back(static_cast<uint8_t>(number));
        return encoded;
    }
    std::vector<uint8_t> bytes;
    while (number > 0) {
        bytes.push_back(static_cast<uint8_t>(number & 0xFF));
        number >>= 8;
    }
    size_t firstChunk = std::min<size_t>(127, bytes.size());
    encoded.push_back(0x80 | static_cast<uint8_t>(firstChunk));
    encoded.insert(encoded.end(), bytes.begin(), bytes.begin() + firstChunk);
    
    size_t offset = firstChunk;
    while (offset < bytes.size()) {
        size_t chunkSize = std::min<size_t>(255, bytes.size() - offset);
        encoded.push_back(static_cast<uint8_t>(chunkSize));
        encoded.insert(encoded.end(), bytes.begin() + offset, bytes.begin() + offset + chunkSize);
        offset += chunkSize;
    }
    return encoded;
}

std::vector<uint8_t> EncoderP::fixedEncodeNumber(long number, int bitCount) {
    int byteCount = (bitCount + 7) / 8;
    std::vector<uint8_t> encoded(byteCount);
    for (int i = 0; i < byteCount; ++i) {
        encoded[i] = number & 0xFF;
        number >>= 8;
    }
    return encoded;
}

std::vector<uint8_t> EncoderP::compressBuffer(const std::vector<uint8_t>& input) {
    if (input.empty()) return {};
    int maxCompressedSize = LZ4_compressBound(input.size());
    std::vector<uint8_t> compressed(maxCompressedSize);
    int compressedSize = LZ4_compress_HC(
        reinterpret_cast<const char*>(input.data()),
        reinterpret_cast<char*>(compressed.data()),
        static_cast<int>(input.size()),
        maxCompressedSize,
        LZ4HC_CLEVEL_MAX
    );
    if (compressedSize <= 0) {
        throw std::runtime_error("LZ4-HC compression failed");
    }
    compressed.resize(compressedSize);
    return compressed;
}

std::vector<uint8_t> EncoderP::generateReferenceCode(ValueType type, long id){
    std::vector<uint8_t> result;
    if(id < 31){
        uint8_t typecode = (type == ValueType::List) ? 0xA0 : 0x80;
        result.push_back(typecode | (id & 0x1F));
    } else {
        uint8_t typecode = (type == ValueType::List) ? 0xBF : 0x9F;
        result.push_back(typecode);
        auto varEncodedId = varEncodeNumber(id);
        result.insert(result.end(), varEncodedId.begin(), varEncodedId.end());
    }
    return result;
}

int EncoderP::nearestBytes(long n) {
    if (n == 0) return 1;
    if (n <= UINT8_MAX) return 1;
    if (n <= UINT16_MAX) return 2;
    if (n <= UINT32_MAX) return 4;
    return 8;
}