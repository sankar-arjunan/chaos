#include "encoder.hpp"
#include "selective_decoder.cpp"
#include "decoder.cpp"
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

// int main(int argc, char* argv[]) {
//     if (argc < 4) {
//         std::cerr << "Usage: " << argv[0] << " input.json output.chaos encodeFlag[y/n] [query...]\n";
//         return 1;
//     }

//     bool isSelective = false;
    
//     if(argc > 4) isSelective = true; 

    

//     std::string inputJsonFile  = argv[1];
//     std::string outputChaosFile = argv[2];
//     std::string isEncodingNeeded = argv[3];

//     std::vector<std::string> query;
//     for (int i = 4; i < argc; i++) {
//         query.push_back(argv[i]);
//     }

//     try {
//         Value rootValue; // declare outside so it’s in scope later

//         auto tStart = std::chrono::high_resolution_clock::now();
//         std::chrono::high_resolution_clock::time_point tParseEnd, tEncodeEnd, tDecodeEnd;

//         if (isEncodingNeeded == "y") {
//             std::ifstream ifs(inputJsonFile);
//             if (!ifs) throw std::runtime_error("Failed to open JSON file: " + inputJsonFile);

//             json j;
//             ifs >> j;

//             rootValue = jsonToValue(j);
//             tParseEnd = std::chrono::high_resolution_clock::now();

//             Encoder encoder;
//             encoder.encode(rootValue, outputChaosFile);

//             tEncodeEnd = std::chrono::high_resolution_clock::now();
//         }

//         MMapDecoderSelective decoder;
//         decoder.setQuery(query);
//         Value outP = decoder.decode(outputChaosFile);

//         tDecodeEnd = std::chrono::high_resolution_clock::now();

//         printValue(outP);

//         bool isMilliSeconds = true;
//         auto endMarker =  isMilliSeconds ? " ms (" : " μs\n";

//         // ---- Timing ----
//         if (isEncodingNeeded == "y") {
//             auto parseTime  = std::chrono::duration_cast<std::chrono::milliseconds>(tParseEnd - tStart).count();
//             auto encodeTime = std::chrono::duration_cast<std::chrono::milliseconds>(tEncodeEnd - tParseEnd).count();
//             auto decodeTime = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeEnd - tEncodeEnd).count();
//             auto totalTime  = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeEnd - tStart).count();

//             std::cout << "\nParse JSON: " << parseTime << endMarker << (parseTime/1000) << " sec)\n";
//             std::cout << "Encode CHAOS: " << encodeTime << endMarker << (encodeTime/1000) << " sec)\n"; 
//             std::cout << "Decode CHAOS: " << decodeTime << endMarker << (decodeTime/1000) << " sec)\n";
//             std::cout << "Total time: " << totalTime << endMarker << (totalTime/1000) << " sec)\n";
//         }
//         else{
//             auto decodeTime = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeEnd - tStart).count();
//             std::cout << "\nDecode CHAOS: " << decodeTime << endMarker << (decodeTime/1000) << " sec)\n";;
//             std::cout << "Total time: " << decodeTime << endMarker << (decodeTime/1000) << " sec)\n";;
//         }

//         std::cout << "Output written to: " << outputChaosFile << "\n";

//     } catch (const std::exception& ex) {
//         std::cerr << "Error: " << ex.what() << "\n";
//         return 1;
//     }

//     return 0;
// }

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " input.json output.chaos encodeFlag[y/n] [query...]\n";
        return 1;
    }

    bool isSelective = false;
    if(argc > 4) isSelective = true; 

    std::string inputJsonFile   = argv[1];
    std::string outputChaosFile = argv[2];
    std::string isEncodingNeeded = argv[3];

    std::vector<std::string> query;
    for (int i = 4; i < argc; i++) query.push_back(argv[i]);

    try {
        Value rootValue; // declare outside so it’s in scope later

        auto tStart = std::chrono::high_resolution_clock::now();
        std::chrono::high_resolution_clock::time_point tParseEnd, tEncodeChaosEnd, tDecodeChaosEnd, tJsonWriteEnd;

        if (isEncodingNeeded == "y") {
            // --- Parse JSON ---
            std::ifstream ifs(inputJsonFile);
            if (!ifs) throw std::runtime_error("Failed to open JSON file: " + inputJsonFile);

            json j;
            ifs >> j;

            rootValue = jsonToValue(j);
            tParseEnd = std::chrono::high_resolution_clock::now();

            // --- Encode to CHAOS ---
            Encoder encoder;
            encoder.encode(rootValue, outputChaosFile);
            tEncodeChaosEnd = std::chrono::high_resolution_clock::now();

            // --- Write Value back to JSON ---
            json outputJson;
            std::function<json(const Value&)> valueToJson = [&](const Value& v) -> json {
                switch (v.type()) {
                    case ValueType::Null:    return nullptr;
                    case ValueType::String:  return std::get<std::string>(v.data);
                    case ValueType::Integer: return std::get<int64_t>(v.data);
                    case ValueType::Float:   return std::get<double>(v.data);
                    case ValueType::Boolean: return std::get<bool>(v.data);
                    case ValueType::Byte:    return (int)std::get<uint8_t>(v.data);
                    case ValueType::Object: {
                        json jObj = json::object();
                        const Object& obj = std::get<Object>(v.data);
                        for (const auto& f : obj.fields) jObj[f.first] = valueToJson(f.second);
                        return jObj;
                    }
                    case ValueType::List: {
                        json jArr = json::array();
                        const List& lst = std::get<List>(v.data);
                        for (const auto& el : lst.elements) jArr.push_back(valueToJson(el));
                        return jArr;
                    }
                    case ValueType::Custom: return "(Custom)"; // optional representation
                    default: return "<unknown>";
                }
            };

            outputJson = valueToJson(rootValue);

            std::string jsonOutputFile = outputChaosFile + ".json";
            std::ofstream ofs(jsonOutputFile);
            if (!ofs) throw std::runtime_error("Failed to open file for JSON output: " + jsonOutputFile);

            auto tJsonStart = std::chrono::high_resolution_clock::now();
            ofs << std::setw(2) << outputJson << std::endl;
            tJsonWriteEnd = std::chrono::high_resolution_clock::now();
        }

        // --- Decode CHAOS ---
        MMapDecoderSelective decoder;
        decoder.setQuery(query);
        Value outP = decoder.decode(outputChaosFile);
        tDecodeChaosEnd = std::chrono::high_resolution_clock::now();

        printValue(outP);

        bool isMilliSeconds = true;
        auto endMarker =  isMilliSeconds ? " ms (" : " μs\n";

        // ---- Timing ----
        if (isEncodingNeeded == "y") {
            auto parseTime      = std::chrono::duration_cast<std::chrono::milliseconds>(tParseEnd - tStart).count();
            auto encodeChaosTime = std::chrono::duration_cast<std::chrono::milliseconds>(tEncodeChaosEnd - tParseEnd).count();
            auto writeJsonTime   = std::chrono::duration_cast<std::chrono::milliseconds>(tJsonWriteEnd - tEncodeChaosEnd).count();
            auto decodeChaosTime = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeChaosEnd - tJsonWriteEnd).count();
            auto totalTime       = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeChaosEnd - tStart).count();

            std::cout << "\nParse JSON: " << parseTime << endMarker << (parseTime/1000) << " sec)\n";
            std::cout << "Encode CHAOS: " << encodeChaosTime << endMarker << (encodeChaosTime/1000) << " sec)\n"; 
            std::cout << "Write back JSON: " << writeJsonTime << endMarker << (writeJsonTime/1000) << " sec)\n";
            std::cout << "Decode CHAOS: " << decodeChaosTime << endMarker << (decodeChaosTime/1000) << " sec)\n";
            std::cout << "Total time: " << totalTime << endMarker << (totalTime/1000) << " sec)\n";

            std::cout << "JSON output written to: " << outputChaosFile << ".json\n";
        }
        else {
            auto decodeTime = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeChaosEnd - tStart).count();
            std::cout << "\nDecode CHAOS: " << decodeTime << endMarker << (decodeTime/1000) << " sec)\n";
            std::cout << "Total time: " << decodeTime << endMarker << (decodeTime/1000) << " sec)\n";
        }

        std::cout << "CHAOS output written to: " << outputChaosFile << "\n";

    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
