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

std::pair<py::object, long long>
chaos_query(const std::string& chaos_file,
            const std::vector<std::vector<std::string>>& queries) {
    MMapDecoderSelective d;
    py::list results;
    auto s = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < queries.size(); ++i) {
        auto q = queries[i];
        d.setQuery(q);
        Value r = (i == 0) ? d.decode(chaos_file) : d.decodeWrapper(0);
        results.append(toPython(r));
    }
    auto e = std::chrono::high_resolution_clock::now();
    return {results, std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count()};
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
    return std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();
}

PYBIND11_MODULE(pychaos, m) {
    m.def("query", &chaos_query, py::arg("chaos_file"), py::arg("queries"));
    m.def("encode", &chaos_encode, py::arg("json_file"), py::arg("chaos_file"));
}
