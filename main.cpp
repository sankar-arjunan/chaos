#include "encoder_parallel.hpp"
#include "encoder.hpp"
#include "decoder.cpp"
#include "decoder_parallel.cpp"
#include "selective_decoder.cpp"
#include "datastruct.hpp"
#include "json.hpp"
#include "simdjson.h"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <functional>
#include <string_view>
#include <filesystem>
#include <vector>
#include <string>
#include <sstream>
#include <ctime>

using json = nlohmann::json;

void printValue(const Value& v, std::ostream& out, int indent);
void printValue(const Value& v, int indent);
Value jsonToValue(const json& j);
Value jsonObjectToObject(const json& j);
Value jsonArrayToList(const json& j);
std::string valueToString(const Value& v);
std::string formatDuration(std::chrono::high_resolution_clock::duration duration);
std::string getCurrentTimestamp();

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
std::string valueToString(const Value& v) {
    std::stringstream ss;
    printValue(v, ss, 0);
    return ss.str();
}

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
    if (j.is_boolean())       return Value(j.get<bool>());
    if (j.is_null())          return Value();
    throw std::runtime_error("Unsupported JSON value type");
}

std::function<json(const Value&)> valueToJson;

std::string formatDuration(std::chrono::high_resolution_clock::duration duration) {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    if (us < 2000) {
        return std::to_string(us) + " µs";
    } else {
        return std::to_string(us / 1000) + " ms";
    }
}

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string buildJsonPointer(const std::vector<std::string>& query_parts) {
    std::string pointer = "";
    if (query_parts.empty()) return pointer;
    for (const auto& part : query_parts) {
        pointer += "/";
        std::string escaped_part = part;
        size_t pos = 0; while ((pos = escaped_part.find('~', pos)) != std::string::npos) { escaped_part.replace(pos, 1, "~0"); pos += 2;}
        pos = 0; while ((pos = escaped_part.find('/', pos)) != std::string::npos) { escaped_part.replace(pos, 1, "~1"); pos += 2;}
        pointer += escaped_part;
    }
    return pointer;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <mode> [options...]\n";
        std::cerr << "Modes:\n";
        std::cerr << "  encode <serial|parallel> <input.json> <output.chaos>\n";
        std::cerr << "  decode <serial|parallel|query> <input.chaos> [query_part1 ... [ | query_part1 ... ] ]\n";
        std::cerr << "  metric <input.json> <output_base.chaos> [query_part1 ... [ | query_part1 ... ] ]\n";
        return 1;
    }

    std::string mode = argv[1];

    try {
        if (mode == "encode") {
            if (argc != 5) {
                std::cerr << "Usage: " << argv[0] << " encode <serial|parallel> <input.json> <output.chaos>\n";
                return 1;
            }
            std::string encoder_type = argv[2];
            std::string inputJsonFile = argv[3];
            std::string outputChaosFile = argv[4];

            auto tStart = std::chrono::high_resolution_clock::now();

            std::ifstream ifs(inputJsonFile);
            if (!ifs) throw std::runtime_error("Failed to open JSON file: " + inputJsonFile);
            json j;
            ifs >> j;

            Value rootValue = jsonToValue(j);
            j = nullptr;

            if (encoder_type == "serial") {
                Encoder encoderS;
                encoderS.encode(rootValue, outputChaosFile);
            } else if (encoder_type == "parallel") {
                EncoderP encoderP;
                encoderP.encode(rootValue, outputChaosFile);
            } else {
                std::cerr << "Invalid encoder type: " << encoder_type << ". Use 'serial' or 'parallel'.\n";
                return 1;
            }

            auto tEnd = std::chrono::high_resolution_clock::now();
            auto duration = tEnd - tStart;

            std::cout << "Encoded '" << inputJsonFile << "' to '" << outputChaosFile
                      << "' using " << encoder_type << " encoder. (" << formatDuration(duration)
                      << ") [" << getCurrentTimestamp() << "]\n";

        } else if (mode == "decode") {
            if (argc < 4) {
                std::cerr << "Usage: " << argv[0] << " decode <serial|parallel|query> <input.chaos> [query...]\n";
                return 1;
            }
            std::string decoder_type = argv[2];
            std::string inputChaosFile = argv[3];

            Value resultValue;
            auto tStart = std::chrono::high_resolution_clock::now();

            if (decoder_type == "serial") {
                MMapDecoder decoder;
                resultValue = decoder.decode(inputChaosFile);
                auto tEnd = std::chrono::high_resolution_clock::now();
                printValue(resultValue, 0);
                std::cout << std::endl;
                std::cout << "Decoded '" << inputChaosFile << "' using " << decoder_type
                          << " decoder. (" << formatDuration(tEnd - tStart) << ") ["
                          << getCurrentTimestamp() << "]\n";

            } else if (decoder_type == "parallel") {
                MMapDecoderParallel decoderP;
                resultValue = decoderP.decode(inputChaosFile);
                auto tEnd = std::chrono::high_resolution_clock::now();
                printValue(resultValue, 0);
                std::cout << std::endl;
                 std::cout << "Decoded '" << inputChaosFile << "' using " << decoder_type
                          << " decoder. (" << formatDuration(tEnd - tStart) << ") ["
                          << getCurrentTimestamp() << "]\n";

            } else if (decoder_type == "query") {
                if (argc < 5) {
                     std::cerr << "Usage: " << argv[0] << " decode query <input.chaos> <query_part1> ... [ | <query_part1> ... ]\n";
                     return 1;
                }

                std::vector<std::vector<std::string>> list_of_queries;
                std::vector<std::string> current_query;
                for (int i = 4; i < argc; ++i) {
                    std::string arg = argv[i];
                    if (arg == "|") {
                        if (!current_query.empty()) {
                            list_of_queries.push_back(current_query);
                            current_query.clear();
                        }
                    } else {
                        current_query.push_back(arg);
                    }
                }
                if (!current_query.empty()) {
                    list_of_queries.push_back(current_query);
                }

                if (list_of_queries.empty()) {
                     std::cerr << "No query parts provided.\n";
                     return 1;
                }

                MMapDecoderSelective decoderS;
                Value firstResult;
                auto tFirstStart = std::chrono::high_resolution_clock::now();
                decoderS.setQuery(list_of_queries[0]);
                firstResult = decoderS.decode(inputChaosFile); 
                auto tFirstEnd = std::chrono::high_resolution_clock::now();

                std::cout << "Query 1 (" << buildJsonPointer(list_of_queries[0]) << "):\n";
                printValue(firstResult, 0);
                std::cout << "\n(" << formatDuration(tFirstEnd - tFirstStart) << ")\n---\n";

                for (size_t i = 1; i < list_of_queries.size(); ++i) {
                    auto tSubsequentStart = std::chrono::high_resolution_clock::now();
                    decoderS.setQuery(list_of_queries[i]);
                    Value subsequentResult = decoderS.decodeWrapper(0); 
                    auto tSubsequentEnd = std::chrono::high_resolution_clock::now();

                    std::cout << "Query " << (i + 1) << " (" << buildJsonPointer(list_of_queries[i]) << "):\n";
                    printValue(subsequentResult, 0);
                    std::cout << "\n(" << formatDuration(tSubsequentEnd - tSubsequentStart) << ")\n---\n";
                }
                 std::cout << "Completed " << list_of_queries.size() << " queries [" << getCurrentTimestamp() << "]\n";


            } else {
                 std::cerr << "Invalid decoder type: " << decoder_type << ". Use 'serial', 'parallel', or 'query'.\n";
                 return 1;
            }

        } else if (mode == "metric") {
             if (argc < 4) {
                std::cerr << "Usage: " << argv[0] << " metric <input.json> <output_base.chaos> [query_part1 ... [ | query_part1 ... ] ]\n";
                return 1;
            }
            std::string inputJsonFile = argv[2];
            std::string outputChaosFileBase = argv[3];

            std::vector<std::vector<std::string>> list_of_queries;
            std::vector<std::string> current_query;
            for (int i = 4; i < argc; ++i) {
                std::string arg = argv[i];
                if (arg == "|") {
                    if (!current_query.empty()) {
                        list_of_queries.push_back(current_query);
                        current_query.clear();
                    }
                } else {
                    current_query.push_back(arg);
                }
            }
            if (!current_query.empty()) {
                list_of_queries.push_back(current_query);
            }

            std::string chaosOutputFileS = outputChaosFileBase + "._s";
            std::string chaosOutputFileP = outputChaosFileBase + "._p";
            std::string jsonOutputFile = outputChaosFileBase + ".json";

            Encoder encoderS;
            EncoderP encoderP;
            MMapDecoder decoder{};
            MMapDecoderParallel decoderP{};
            MMapDecoderSelective decoderS{};

            std::ifstream ifs(inputJsonFile);
            if (!ifs) throw std::runtime_error("Failed to open JSON file: " + inputJsonFile);
            json j;
            ifs >> j;

            auto tStart = std::chrono::high_resolution_clock::now();
            Value rootValue = jsonToValue(j);
            j = nullptr;
            auto tParseEnd = std::chrono::high_resolution_clock::now();

            encoderP.encode(rootValue, chaosOutputFileP);
            auto tEncodeChaosEndP = std::chrono::high_resolution_clock::now();

            encoderS.encode(rootValue, chaosOutputFileS);
            auto tEncodeChaosEndS = std::chrono::high_resolution_clock::now();

            valueToJson = [&](const Value& v) -> json {
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
                    case ValueType::Custom: return "(Custom)";
                    default: return "<unknown>";
                }
            };

            json outputJson = valueToJson(rootValue);
            std::ofstream ofs(jsonOutputFile);
            if (!ofs) throw std::runtime_error("Failed to open file for JSON output: " + jsonOutputFile);
            ofs << std::setw(2) << outputJson << std::endl;
            outputJson = nullptr;
            auto tJsonWriteEnd = std::chrono::high_resolution_clock::now();

            auto outP = decoder.decode(chaosOutputFileS);
            auto tDecodeChaosEnd = std::chrono::high_resolution_clock::now();

            auto outP2 = decoderP.decode(chaosOutputFileP);
            auto tDecodeChaosEnd2 = std::chrono::high_resolution_clock::now();

            Value firstChaosQueryResult;
            std::vector<Value> subsequentChaosQueryResults;
            std::vector<long long> chaosQueryTimesMs;
            std::chrono::high_resolution_clock::time_point tChaosQueryEnd = tDecodeChaosEnd2; 

            if (!list_of_queries.empty()) {
                auto tChaosQueryStart = std::chrono::high_resolution_clock::now();
                decoderS.setQuery(list_of_queries[0]);
                firstChaosQueryResult = decoderS.decode(chaosOutputFileS); 
                tChaosQueryEnd = std::chrono::high_resolution_clock::now();
                chaosQueryTimesMs.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(tChaosQueryEnd - tChaosQueryStart).count());

                for (size_t i = 1; i < list_of_queries.size(); ++i) {
                     tChaosQueryStart = std::chrono::high_resolution_clock::now();
                     decoderS.setQuery(list_of_queries[i]);
                     subsequentChaosQueryResults.push_back(decoderS.decodeWrapper(0));
                     tChaosQueryEnd = std::chrono::high_resolution_clock::now();
                     chaosQueryTimesMs.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(tChaosQueryEnd - tChaosQueryStart).count());
                }
            }


            std::vector<std::string> simdjson_results_str;
            std::vector<long long> simdjsonQueryTimesMs;
            std::chrono::high_resolution_clock::time_point tSimdjsonEnd = tChaosQueryEnd; 

            if (!list_of_queries.empty()) {
                simdjson::ondemand::parser parser;
                simdjson::padded_string json_data = simdjson::padded_string::load(inputJsonFile); 
                simdjson::ondemand::document doc;

                for(const auto& current_query_parts : list_of_queries) {
                    std::string json_pointer_query = buildJsonPointer(current_query_parts);
                    std::string current_result_str = "N/A";
                    auto tSimdjsonStart = std::chrono::high_resolution_clock::now();
                    try {
                        doc = parser.iterate(json_data); 
                        auto found_val = doc.at_pointer(std::string_view(json_pointer_query));
                        current_result_str = std::string(simdjson::to_json_string(found_val).value());
                    } catch (const simdjson::simdjson_error& e) {
                         current_result_str = "Query path not found or error: " + std::string(e.what());
                    }
                    tSimdjsonEnd = std::chrono::high_resolution_clock::now();
                    simdjson_results_str.push_back(current_result_str);
                    simdjsonQueryTimesMs.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(tSimdjsonEnd - tSimdjsonStart).count());
                }
            }


            auto parseTime         = std::chrono::duration_cast<std::chrono::milliseconds>(tParseEnd - tStart).count();
            auto encodeTimeP       = std::chrono::duration_cast<std::chrono::milliseconds>(tEncodeChaosEndP - tParseEnd).count();
            auto encodeTimeS       = std::chrono::duration_cast<std::chrono::milliseconds>(tEncodeChaosEndS - tEncodeChaosEndP).count();
            auto writeJsonTime     = std::chrono::duration_cast<std::chrono::milliseconds>(tJsonWriteEnd - tEncodeChaosEndS).count();
            auto decodeTime        = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeChaosEnd - tJsonWriteEnd).count();
            auto decodeTimeP       = std::chrono::duration_cast<std::chrono::milliseconds>(tDecodeChaosEnd2 - tDecodeChaosEnd).count();
            
            auto decodeTimeS_first = chaosQueryTimesMs.empty() ? -1 : chaosQueryTimesMs[0];
            auto totalTime         = std::chrono::duration_cast<std::chrono::milliseconds>(tSimdjsonEnd - tStart).count();


            uintmax_t json_size = 0;
            uintmax_t chaos_size_s = 0;
            uintmax_t chaos_size_p = 0;
            double ratio_s = 0.0, ratio_p = 0.0;
            try {
                json_size = std::filesystem::file_size(inputJsonFile);
                if(std::filesystem::exists(chaosOutputFileS)) chaos_size_s = std::filesystem::file_size(chaosOutputFileS);
                if(std::filesystem::exists(chaosOutputFileP)) chaos_size_p = std::filesystem::file_size(chaosOutputFileP);
                if (json_size > 0) {
                     if(chaos_size_s > 0) ratio_s = static_cast<double>(chaos_size_s) / json_size;
                     if(chaos_size_p > 0) ratio_p = static_cast<double>(chaos_size_p) / json_size;
                }
            } catch (const std::filesystem::filesystem_error& e) {
                 std::cerr << "Warning: Could not get file size - " << e.what() << '\n';
            }

            json results_json = json::object();
            results_json["metrics"] = {
                {"json-parse-nlohmann-ms", parseTime},
                {"json-encode-nlohmann-ms", writeJsonTime},
                {"chaos-encode-serial-ms", encodeTimeS},
                {"chaos-encode-parallel-ms", encodeTimeP},
                {"chaos-decode-serial-ms", decodeTime},
                {"chaos-decode-parallel-ms", decodeTimeP},
                {"chaos-decode-selective-first-ms", decodeTimeS_first},
                {"json-query-simdjson-first-ms", simdjsonQueryTimesMs.empty() ? -1 : simdjsonQueryTimesMs[0]},
                {"total-time-ms", totalTime}
            };

            if (chaosQueryTimesMs.size() > 1) {
                json subsequent_chaos_times = json::array();
                for(size_t i=1; i<chaosQueryTimesMs.size(); ++i) subsequent_chaos_times.push_back(chaosQueryTimesMs[i]);
                results_json["metrics"]["chaos-decode-selective-subsequent-ms"] = subsequent_chaos_times;
            }
             if (simdjsonQueryTimesMs.size() > 1) {
                json subsequent_simdjson_times = json::array();
                for(size_t i=1; i<simdjsonQueryTimesMs.size(); ++i) subsequent_simdjson_times.push_back(simdjsonQueryTimesMs[i]);
                results_json["metrics"]["json-query-simdjson-subsequent-ms"] = subsequent_simdjson_times;
            }


            results_json["sizes"] = {
                {"json-bytes", json_size},
                {"chaos-serial-bytes", chaos_size_s},
                {"chaos-parallel-bytes", chaos_size_p},
                {"chaos-ratio-serial", ratio_s},
                {"chaos-ratio-parallel", ratio_p}
            };

            json query_results = json::array(); 
            for(size_t i = 0; i < list_of_queries.size(); ++i) {
                json single_query_result = {
                     {"path", list_of_queries[i]},
                     {"result-json-simdjson", (i < simdjson_results_str.size()) ? simdjson_results_str[i] : "N/A"}
                };
                 if (i == 0) {
                     single_query_result["result-chaos-selective"] = (firstChaosQueryResult.type() != ValueType::Null) ? valueToString(firstChaosQueryResult) : "Query path not found or returned null";
                 } else if ((i-1) < subsequentChaosQueryResults.size()) {
                      single_query_result["result-chaos-selective"] = (subsequentChaosQueryResults[i-1].type() != ValueType::Null) ? valueToString(subsequentChaosQueryResults[i-1]) : "Query path not found or returned null";
                 } else {
                      single_query_result["result-chaos-selective"] = "N/A";
                 }
                 query_results.push_back(single_query_result);
            }
             if (list_of_queries.empty()) {
                 results_json["query"] = {
                    {"path", json::array()},
                    {"result-json-simdjson", "N/A"},
                    {"result-chaos-selective", "N/A"}
                };
             } else {
                 results_json["query_results"] = query_results; 
             }


            results_json["output-files"] = {
                {"chaos-serial", chaosOutputFileS},
                {"chaos-parallel", chaosOutputFileP},
                {"json-written-back", jsonOutputFile}
            };

            std::cout << std::setw(2) << results_json << std::endl;

        } else {
            std::cerr << "Invalid mode: " << mode << ". Use 'encode', 'decode', or 'metric'.\n";
            return 1;
        }

    } catch (const std::exception& ex) {
        json error_json = { {"error", ex.what()} };
        std::cerr << std::setw(2) << error_json << std::endl;
        return 1;
    }

    return 0;
}