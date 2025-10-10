#include "encoder.hpp"

void Encoder::encode(const Value& root, const std::string& filename) {
    std::vector<uint8_t> output;
    std::vector<std::pair<long, Value>> stack;
    
    output.reserve(1024 * 1024); 

    stack.push_back({0, root});
    currentEntityId = 1;

    while(!stack.empty()){
        auto [id, value] = stack.back();
        stack.pop_back();

        std::vector<std::pair<long, Value>> children;
        encodeValue(value, id, output, children);

        for(int i = children.size() - 1; i >= 0; --i){
            stack.push_back(children[i]);
        }
    }

    std::vector<uint8_t> header;
    header.reserve(4096);

    auto varEncodedEntityCount = varEncodeNumber(currentEntityId);
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

    for (uint32_t eid = 0; eid < currentEntityId; ++eid) {
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

void Encoder::encodeValue(const Value& value, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children) {
    if (value.type() == ValueType::Object) {
        encodeObject(std::get<Object>(value.data), id, output, children);
    } else if (value.type() == ValueType::List) {
        encodeList(std::get<List>(value.data), id, output, children);
    }
}

std::vector<uint8_t> Encoder::encodeKey(const std::string& key){
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

void Encoder::encodePrimitive(const Value& value, std::vector<uint8_t>& out) {
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

void Encoder::encodeList(const List& entity, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children) {
    entityOffsetTable[id] = output.size();

    std::vector<uint8_t> dataValue;
    std::vector<long> offsetTableLong;

    for (const auto& value : entity.elements) {
        offsetTableLong.push_back(dataValue.size());
        if (value.type() == ValueType::List || value.type() == ValueType::Object) {
            long childId = currentEntityId++;
            auto referenceCode = generateReferenceCode(value.type(), childId);
            dataValue.insert(dataValue.end(), referenceCode.begin(), referenceCode.end());
            children.push_back({childId, value});
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
}

void Encoder::encodeObject(const Object& entity, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children) {
    entityOffsetTable[id] = output.size();

    std::vector<uint8_t> dataValue;
    std::vector<long> offsetTableLong;

    for (const auto& kvPair : entity.fields) {
        offsetTableLong.push_back(dataValue.size());
        
        auto encodedKey = encodeKey(kvPair.first);
        dataValue.insert(dataValue.end(), encodedKey.begin(), encodedKey.end());

        const auto& value = kvPair.second;
        if (value.type() == ValueType::List || value.type() == ValueType::Object) {
            long childId = currentEntityId++;
            auto referenceCode = generateReferenceCode(value.type(), childId);
            dataValue.insert(dataValue.end(), referenceCode.begin(), referenceCode.end());
            children.push_back({childId, value});
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
}

std::vector<uint8_t> Encoder::varEncodeNumber(uint64_t number) {
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

std::vector<uint8_t> Encoder::fixedEncodeNumber(long number, int bitCount) {
    int byteCount = (bitCount + 7) / 8;
    std::vector<uint8_t> encoded(byteCount);
    for (int i = 0; i < byteCount; ++i) {
        encoded[i] = number & 0xFF;
        number >>= 8;
    }
    return encoded;
}

std::vector<uint8_t> Encoder::compressBuffer(const std::vector<uint8_t>& input) {
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

std::vector<uint8_t> Encoder::generateReferenceCode(ValueType type, long id){
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

int Encoder::nearestBytes(long n) {
    if (n == 0) return 1;
    if (n <= UINT8_MAX) return 1;
    if (n <= UINT16_MAX) return 2;
    if (n <= UINT32_MAX) return 4;
    return 8;
}