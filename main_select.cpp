#include "encoder.hpp"
#include "selective_decoder.cpp"
#include "datastruct.hpp"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>


void printValue(const Value& v, int indent = 0);

// helper to print indentation spaces
static void printIndent(int indent) {
    for (int i = 0; i < indent; ++i) std::cout << ' ';
}

void printValue(const Value& v, int indent) {
    ValueType type = v.type();

    switch (type) {
        case ValueType::Null:
            std::cout << "null";
            break;

        case ValueType::String:
            std::cout << '"' << std::get<std::string>(v.data) << '"';
            break;

        case ValueType::Integer:
            std::cout << std::get<int64_t>(v.data);
            break;

        case ValueType::Float:
            std::cout << std::fixed << std::setprecision(6)
                      << std::get<double>(v.data);
            break;

        case ValueType::Boolean:
            std::cout << (std::get<bool>(v.data) ? "true" : "false");
            break;

        case ValueType::Byte:
            std::cout << "(byte) " << (int)std::get<uint8_t>(v.data);
            break;

        case ValueType::Object: {
            const Object& obj = std::get<Object>(v.data);
            std::cout << "{\n";
            for (size_t i = 0; i < obj.fields.size(); ++i) {
                printIndent(indent + 2);
                std::cout << obj.fields[i].first << ": ";
                printValue(obj.fields[i].second, indent + 2);
                if (i + 1 < obj.fields.size()) std::cout << ",";
                std::cout << "\n";
            }
            printIndent(indent);
            std::cout << "}";
            break;
        }

        case ValueType::List: {
            const List& list = std::get<List>(v.data);
            std::cout << "[\n";
            for (size_t i = 0; i < list.elements.size(); ++i) {
                printIndent(indent + 2);
                printValue(list.elements[i], indent + 2);
                if (i + 1 < list.elements.size()) std::cout << ",";
                std::cout << "\n";
            }
            printIndent(indent);
            std::cout << "]";
            break;
        }

        case ValueType::Custom: {
            const Custom& c = std::get<Custom>(v.data);
            std::cout << "(Custom id=" << (int)c.id
                      << ", data=" << c.data.size() << " bytes)";
            break;
        }

        default:
            std::cout << "<unknown>";
            break;
    }
}


using json = nlohmann::json;

// Forward declarations
Value jsonToValue(const json& j);

// JSON → Value
Value jsonObjectToObject(const json& j) {
    Object obj;
    for (auto it = j.begin(); it != j.end(); ++it) {
        obj.add(it.key(), jsonToValue(it.value()));
    }
    return obj.toValue();
}

Value jsonArrayToList(const json& j) {
    List lst;
    for (const auto& el : j) {
        lst.add(jsonToValue(el));
    }
    return lst.toValue();
}

Value jsonToValue(const json& j) {
    if (j.is_object()) return jsonObjectToObject(j);
    if (j.is_array())  return jsonArrayToList(j);
    if (j.is_string()) return Value(j.get<std::string>());
    if (j.is_number_integer()) return Value((int64_t)j.get<int64_t>());
    if (j.is_number_float())   return Value(j.get<double>());
    if (j.is_boolean())        return Value(j.get<bool>());
    if (j.is_null())           return Value();
    throw std::runtime_error("Unsupported JSON value type");
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " input.json output.chaos\n";
        return 1;
    }

    std::string inputJsonFile  = argv[1];
    std::string outputChaosFile = argv[2];

    try {

        // ------------------ Encode ------------------
        Encoder encoder; // block size
        MMapDecoderSelective decoder;

        std::vector<std::string> q = {"3", "employee", "projects", "0", "tasks", "1", "details", "technologiesUsed", "2"};

        decoder.setQuery(q);

        std::ifstream ifs(inputJsonFile);
        if (!ifs) throw std::runtime_error("Failed to open JSON file: " + inputJsonFile);

        json j;
        ifs >> j;

        auto tStart = std::chrono::high_resolution_clock::now();

        Value rootValue = jsonToValue(j);

        auto tParseEnd = std::chrono::high_resolution_clock::now();

        encoder.encode(rootValue, outputChaosFile);

        auto tEncodeEnd = std::chrono::high_resolution_clock::now();

        auto outP = decoder.decode(outputChaosFile);

        auto tDecodeEnd = std::chrono::high_resolution_clock::now();

        printValue(rootValue);
        printValue(outP);
        // --------------   ---- Print timings ------------------
        auto parseTime  = std::chrono::duration_cast<std::chrono::milliseconds>(tParseEnd - tStart).count();
        auto encodeTime = std::chrono::duration_cast<std::chrono::milliseconds>(tEncodeEnd - tParseEnd).count();
        auto decodeTime = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeEnd - tEncodeEnd).count();
        auto totalTime  = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeEnd - tStart).count();

        std::cout << "Parse JSON: " << parseTime << " ms\n";
        std::cout << "Encode CHAOS: " << encodeTime << " ms\n";
        std::cout << "Decode CHAOS: " << decodeTime << " ms\n";
        std::cout << "Total time: " << totalTime << " ms\n";
        std::cout << "Output written to: " << outputChaosFile << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
