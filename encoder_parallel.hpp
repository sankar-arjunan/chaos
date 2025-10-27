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
#include <atomic>
#include <mutex>
#include <thread>
#include <queue>
#include <functional>
#include <condition_variable>
#include <future>

class EncoderP {
public:
    EncoderP();
    ~EncoderP();
    
    void encode(const Value& root, const std::string& filename);

private:
    std::vector<std::thread> pool_workers;
    std::queue<std::packaged_task<std::pair<long, std::vector<uint8_t>>()>> pool_tasks;
    std::mutex pool_mutex;
    std::condition_variable pool_cv;
    std::atomic<bool> pool_stop;

    void init_pool();
    void stop_pool();
    std::future<std::pair<long, std::vector<uint8_t>>> enqueue_task(std::function<std::pair<long, std::vector<uint8_t>>()> task_func);

    std::atomic<long> currentEntityId;
    std::unordered_map<long, long> entityOffsetTable;
    std::mutex dictionary_mutex;

    std::vector<std::string> dictionary_list;
    std::unordered_map<std::string, uint64_t> dictionary_map;

    void encodeValue(const Value& value, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children);
    void encodeList(const List& entity, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children);
    void encodeObject(const Object& entity, long id, std::vector<uint8_t>& output, std::vector<std::pair<long, Value>>& children);
    std::vector<uint8_t> encodeKey(const std::string& key);

    void serial_build_key(const std::string& key);
    std::vector<uint8_t> get_key_encoding(const std::string& key);
    std::vector<uint8_t> parallel_encode_list(const List& entity, const std::map<const Value*, long>& id_map);
    std::vector<uint8_t> parallel_encode_object(const Object& entity, const std::map<const Value*, long>& id_map);
    std::pair<long, std::vector<uint8_t>> parallel_encode_value(const Value* value, long id, const std::map<const Value*, long>* id_map);
    
    void encodePrimitive(const Value& value, std::vector<uint8_t>& out);
    std::vector<uint8_t> generateReferenceCode(ValueType type, long id);
    std::vector<uint8_t> varEncodeNumber(uint64_t number);
    std::vector<uint8_t> fixedEncodeNumber(long number, int bitCount);
    std::vector<uint8_t> compressBuffer(const std::vector<uint8_t>& input);
    int nearestBytes(long n);
};