#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "selective_decoder.cpp"
#include "datastruct.hpp"
#include "encoder_parallel.hpp"
#include "json.hpp"
#include "decoder_parallel.cpp"

namespace py = pybind11;
using json = nlohmann::json;

py::object toPython(const Value& v) {
    switch (v.type()) {
        case ValueType::Null: return py::none();
        case ValueType::String: return py::str(std::get<std::string>(v.data));
        case ValueType::Integer: return py::int_(std::get<int64_t>(v.data));
        case ValueType::Float: return py::float_(std::get<double>(v.data));
        case ValueType::Boolean: return py::bool_(std::get<bool>(v.data));
        case ValueType::Byte: return py::int_(std::get<uint8_t>(v.data));
        case ValueType::Object: {
            const Object& o = std::get<Object>(v.data);
            py::dict d;
            for (auto& [k, val] : o.fields) d[py::str(k)] = toPython(val);
            return d;
        }
        case ValueType::List: {
            const List& lst = std::get<List>(v.data);
            py::list l;
            for (auto& el : lst.elements) l.append(toPython(el));
            return l;
        }
        case ValueType::Custom: {
            const Custom& c = std::get<Custom>(v.data);
            std::ostringstream oss;
            for (auto b : c.data)
                oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
            return py::bytes(oss.str());
        }
        default: return py::str("<unknown>");
    }
}

Value jsonToValue(const json& j) {
    if (j.is_object()) {
        Object o;
        for (auto it = j.begin(); it != j.end(); ++it)
            o.add(it.key(), jsonToValue(it.value()));
        return o.toValue();
    }
    if (j.is_array()) {
        List l;
        for (auto& el : j) l.add(jsonToValue(el));
        return l.toValue();
    }
    if (j.is_string()) return Value(j.get<std::string>());
    if (j.is_number_integer()) return Value((int64_t)j.get<int64_t>());
    if (j.is_number_float()) return Value(j.get<double>());
    if (j.is_boolean()) return Value(j.get<bool>());
    if (j.is_null()) return Value();
    throw std::runtime_error("Unsupported JSON value type");
}

// chaos_bindings_fixes.cpp

py::object chaos_load(const std::string& chaos_file) {
    auto* decoder_ptr = new MMapDecoderSelective();
    decoder_ptr->load(chaos_file);
    return py::cast(decoder_ptr, py::return_value_policy::take_ownership);
}

std::tuple<py::object, long long>
chaos_query(const std::string& chaos_file,
            const std::vector<std::vector<std::string>>& queries,
            py::object existing_decoder = py::none())
{
    MMapDecoderSelective* decoder_ptr = nullptr;
    std::unique_ptr<MMapDecoderSelective> owned_decoder; // if we create one, we own it

    if (existing_decoder.is_none()) {
        owned_decoder.reset(new MMapDecoderSelective());
        owned_decoder->load(chaos_file);
        decoder_ptr = owned_decoder.get();
    } else {
        decoder_ptr = existing_decoder.cast<MMapDecoderSelective*>();
        if (!decoder_ptr) throw std::runtime_error("Invalid decoder object passed");
    }

    py::list results;
    auto s = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < queries.size(); ++i) {
        const auto& q = queries[i];
        decoder_ptr->setQuery(const_cast<std::vector<std::string>&>(const_cast<std::vector<std::string>&>(q)));
        Value r;
        // first query must call decode() only if we created and haven't previously used this decoder to query
        // safe behavior: if owned_decoder is non-null -> do a decodeWrapper(0) after load, selective traversal works
        // but decodeWrapper expects entityTable/baseOffset to be set (they are after load)
        r = decoder_ptr->decodeWrapper(0);
        results.append(toPython(r));
    }

    auto e = std::chrono::high_resolution_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::microseconds>(e - s).count();

    return std::make_tuple(results, ms);
}

std::tuple<py::object, long long>
chaos_keys(const std::string& chaos_file,
           const std::vector<std::vector<std::string>>& queries,
           py::object existing_decoder = py::none())
{
    MMapDecoderSelective* decoder_ptr = nullptr;
    std::unique_ptr<MMapDecoderSelective> owned_decoder;

    if (existing_decoder.is_none()) {
        owned_decoder.reset(new MMapDecoderSelective());
        owned_decoder->load(chaos_file);
        decoder_ptr = owned_decoder.get();
    } else {
        decoder_ptr = existing_decoder.cast<MMapDecoderSelective*>();
        if (!decoder_ptr) throw std::runtime_error("Invalid decoder object passed");
    }

    py::list results;
    auto s = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < queries.size(); ++i) {
        const auto& q = queries[i];
        decoder_ptr->setQuery(const_cast<std::vector<std::string>&>(const_cast<std::vector<std::string>&>(q)));
        // ensure queryOffset starts at 0 inside setQuery (your setQuery does this)
        // ensure decodeWrapper reads from baseOffset/entityTable (load() populated these)
        Value r = decoder_ptr->getKeys();
        results.append(toPython(r));
    }

    auto e = std::chrono::high_resolution_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::microseconds>(e - s).count();

    return std::make_tuple(results, ms);
}

std::tuple<py::object, long long>
chaos_len(const std::string& chaos_file,
          const std::vector<std::vector<std::string>>& queries,
          py::object existing_decoder = py::none())
{
    MMapDecoderSelective* decoder_ptr = nullptr;
    std::unique_ptr<MMapDecoderSelective> owned_decoder;

    if (existing_decoder.is_none()) {
        owned_decoder.reset(new MMapDecoderSelective());
        owned_decoder->load(chaos_file);
        decoder_ptr = owned_decoder.get();
    } else {
        decoder_ptr = existing_decoder.cast<MMapDecoderSelective*>();
        if (!decoder_ptr) throw std::runtime_error("Invalid decoder object passed");
    }

    py::list results;
    auto s = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < queries.size(); ++i) {
        const auto& q = queries[i];
        decoder_ptr->setQuery(const_cast<std::vector<std::string>&>(const_cast<std::vector<std::string>&>(q)));
        Value r = decoder_ptr->getLen();
        results.append(toPython(r));
    }

    auto e = std::chrono::high_resolution_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::microseconds>(e - s).count();

    return std::make_tuple(results, ms);
}



long long chaos_encode(const std::string& json_file, const std::string& chaos_file) {
    std::ifstream ifs(json_file);
    if (!ifs) throw std::runtime_error("Failed to open " + json_file);
    json j; ifs >> j;
    Value root = jsonToValue(j);
    EncoderP enc;
    auto s = std::chrono::high_resolution_clock::now();
    enc.encode(root, chaos_file);
    auto e = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(e - s).count();
}

std::pair<py::object, unsigned long long> chaos_decode(const std::string& chaos_file) {
    MMapDecoderParallel d;
    auto s = std::chrono::high_resolution_clock::now();
    Value v = d.decode(chaos_file);
    auto e = std::chrono::high_resolution_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::microseconds>(e - s).count();
    return {toPython(v), ms};
}


PYBIND11_MODULE(pychaos, m) {
    py::class_<MMapDecoderSelective>(m, "Decoder");
    m.def("query", &chaos_query,
          py::arg("chaos_file"),
          py::arg("queries"),
          py::arg("decoder")
    );
    
    m.def("len", &chaos_len,
          py::arg("chaos_file"),
          py::arg("queries"),
          py::arg("decoder")
    );

    m.def("keys", &chaos_keys,
          py::arg("chaos_file"),
          py::arg("queries"),
          py::arg("decoder")
    );


    m.def("encode", &chaos_encode, py::arg("json_file"), py::arg("chaos_file"));
    m.def("decode", &chaos_decode, py::arg("chaos_file"));
    m.def("load", &chaos_load, py::arg("chaos_load"));
}

