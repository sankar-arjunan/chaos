#pragma once

#include "datastruct.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <variant>
#include <utility>
#include <cfloat>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <map>
#include <lz4.h>
#include <lz4hc.h>

class Encoder {
public:
    Encoder() : currentEntityId(0), masterOffset(0) {}
    void encode(const Value& root, const std::string& filename);

private:
    uint64_t currentEntityId;
    uint64_t masterOffset;
    std::unordered_map<long, long> entityOffsetTable;

    std::vector<std::string> dictionary_list;
    std::unordered_map<std::string, uint64_t> dictionary_map;

    void encodeValue(const Value& value, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children);
    void encodePrimitive(const Value& value, std::vector<uint8_t>& out);
    void encodeList(const List& entity, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children);
    void encodeObject(const Object& entity, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children);
    
    std::vector<uint8_t> generateReferenceCode(ValueType type, long id);
    std::vector<uint8_t> encodeKey(const std::string& key);
    
    std::vector<uint8_t> varEncodeNumber(uint64_t number);
    std::vector<uint8_t> fixedEncodeNumber(long number, int bitCount);
    std::vector<uint8_t> compressBuffer(const std::vector<uint8_t>& input);
    int nearestBytes(long n);
};