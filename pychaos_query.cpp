#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <chrono>
#include <sstream>
#include <iomanip>

#include "selective_decoder.cpp"
#include "datastruct.hpp"


namespace py = pybind11;

void printValue(const Value& v, std::ostream& out, int indent) {
    ValueType type = v.type();
    auto printIndentStream = [&](int ind) {
        for (int i = 0; i < ind; ++i) out << ' ';
    };

    switch (type) {
        case ValueType::Null:    out << "null"; break;
        case ValueType::String:  out << '"' << std::get<std::string>(v.data) << '"'; break;
        case ValueType::Integer: out << std::get<int64_t>(v.data); break;
        case ValueType::Float:   out << std::fixed << std::setprecision(6) << std::get<double>(v.data); break;
        case ValueType::Boolean: out << (std::get<bool>(v.data) ? "true" : "false"); break;
        case ValueType::Byte:    out << "(byte) " << (int)std::get<uint8_t>(v.data); break;
        case ValueType::Object: {
            const Object& obj = std::get<Object>(v.data);
            out << "{\n";
            for (size_t i = 0; i < obj.fields.size(); ++i) {
                printIndentStream(indent + 2);
                out << obj.fields[i].first << ": ";
                printValue(obj.fields[i].second, out, indent + 2);
                if (i + 1 < obj.fields.size()) out << ",";
                out << "\n";
            }
            printIndentStream(indent);
            out << "}";
            break;
        }
        case ValueType::List: {
             const List& list = std::get<List>(v.data);
             out << "[\n";
             for (size_t i = 0; i < list.elements.size(); ++i) {
                 printIndentStream(indent + 2);
                 printValue(list.elements[i], out, indent + 2);
                 if (i + 1 < list.elements.size()) out << ",";
                 out << "\n";
             }
             printIndentStream(indent);
             out << "]";
             break;
         }
         case ValueType::Custom: {
             const Custom& c = std::get<Custom>(v.data);
             out << "(Custom id=" << (int)c.id << ", data=" << c.data.size() << " bytes)";
             break;
         }
        default: out << "<unknown>"; break;
    }
}
void printValue(const Value& v, int indent) {
    printValue(v, std::cout, indent);
}



// Helper: convert Value to string (reuse your printValue logic)
std::string valueToString(const Value& v) {
    std::ostringstream oss;
    printValue(v, 0); // if you have this function from CLI
    return oss.str();
}



std::pair<std::string, long long>
chaos_query(const std::string& chaos_file,
            const std::vector<std::vector<std::string>>& queries)
{
    MMapDecoderSelective decoder;
    std::ostringstream out;
    auto start = std::chrono::high_resolution_clock::now();

    for (size_t i = 0; i < queries.size(); ++i) {
        auto q = queries[i];  
        decoder.setQuery(q);
        Value res = (i == 0)
            ? decoder.decode(chaos_file)
            : decoder.decodeWrapper(0);
        printValue(res, out, 0);
    }

    auto end = std::chrono::high_resolution_clock::now();
    long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    return {out.str(), ms};
}

PYBIND11_MODULE(pychaos, m) {
    m.doc() = "Minimal CHAOS Python binding for selective queries";
    m.def("query", &chaos_query,
          "Run selective queries on a CHAOS file",
          py::arg("chaos_file"),
          py::arg("queries"));
}
