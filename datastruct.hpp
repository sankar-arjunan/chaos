#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <variant>
#include <utility>
#include <stdexcept> 

struct Value;
struct Object;
struct List;
struct Custom;
struct Reference;


struct Object {
    std::vector<std::pair<std::string, Value>> fields;
    void add(const std::string& key, const Value& v);
    Value toValue() const;
};

struct List {
    std::vector<Value> elements;
    void add(const Value& v);
    Value toValue() const;
};

struct Custom {
    uint8_t id;
    std::vector<uint8_t> data;

    Custom() : id(0) {}
    Custom(uint8_t _id, std::vector<uint8_t>&& _data)
        : id(_id), data(std::move(_data)) {}

    Value toValue() const;
};

struct Reference {
    long id;
    Reference(long _id) : id(_id) {}
    Value toValue() const;
};


using ValueVariant = std::variant<
    std::monostate, 
    std::string,
    int64_t,
    double,
    bool,
    uint8_t,
    Object,
    List,
    Custom,
    Reference
>;

enum class ValueType : uint8_t {
    Null,
    String,
    Integer,
    Float,
    Boolean,
    Byte,
    Object,
    List,
    Custom,
    Reference
};


struct Value {
    ValueVariant data;

    Value() : data(std::monostate{}) {}
    Value(int v) : data((int64_t)v) {}
    Value(int64_t v) : data(v) {}
    Value(double v) : data(v) {}
    Value(const std::string& v) : data(v) {}
    Value(std::string&& v) : data(std::move(v)) {}
    Value(const char* v) : data(std::string(v)) {}
    Value(bool v) : data(v) {}
    Value(uint8_t v) : data(v) {}
    Value(const Object& v) : data(v) {}
    Value(Object&& v) : data(std::move(v)) {}
    Value(const List& v) : data(v) {}
    Value(List&& v) : data(std::move(v)) {}
    Value(const Custom& v) : data(v) {}
    Value(Custom&& v) : data(std::move(v)) {}
    
    Value(const Reference& v) : data(v) {}
    Value(Reference&& v) : data(std::move(v)) {}

    ValueType type() const {
        return std::visit([](auto&& arg) -> ValueType {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::monostate>) return ValueType::Null;
            else if constexpr (std::is_same_v<T, std::string>) return ValueType::String;
            else if constexpr (std::is_same_v<T, int64_t>) return ValueType::Integer;
            else if constexpr (std::is_same_v<T, double>) return ValueType::Float;
            else if constexpr (std::is_same_v<T, bool>) return ValueType::Boolean;
            else if constexpr (std::is_same_v<T, uint8_t>) return ValueType::Byte;
            else if constexpr (std::is_same_v<T, Object>) return ValueType::Object;
            else if constexpr (std::is_same_v<T, List>) return ValueType::List;
            else if constexpr (std::is_same_v<T, Custom>) return ValueType::Custom;
            else if constexpr (std::is_same_v<T, Reference>) return ValueType::Reference;
            else return ValueType::Null;
        }, data);
    }

    bool isNull() const { return type() == ValueType::Null; }
    bool isString() const { return type() == ValueType::String; }
    bool isInteger() const { return type() == ValueType::Integer; }
    bool isFloat() const { return type() == ValueType::Float; }
    bool isBoolean() const { return type() == ValueType::Boolean; }
    bool isByte() const { return type() == ValueType::Byte; }
    bool isObject() const { return type() == ValueType::Object; }
    bool isList() const { return type() == ValueType::List; }
    bool isCustom() const { return type() == ValueType::Custom; }
    bool isReference() const { return type() == ValueType::Reference; }

    const std::string& asString() const { if(!isString()) throw std::runtime_error("Not a String"); return std::get<std::string>(data); }
    int64_t asInteger() const { if(!isInteger()) throw std::runtime_error("Not an Integer"); return std::get<int64_t>(data); }
    double asFloat() const { if(!isFloat()) throw std::runtime_error("Not a Float"); return std::get<double>(data); }
    bool asBoolean() const { if(!isBoolean()) throw std::runtime_error("Not a Boolean"); return std::get<bool>(data); }
    uint8_t asByte() const { if(!isByte()) throw std::runtime_error("Not a Byte"); return std::get<uint8_t>(data); }
    const Object& asObject() const { if(!isObject()) throw std::runtime_error("Not an Object"); return std::get<Object>(data); }
    Object& asObject() { if(!isObject()) throw std::runtime_error("Not an Object"); return std::get<Object>(data); }
    const List& asList() const { if(!isList()) throw std::runtime_error("Not a List"); return std::get<List>(data); }
    List& asList() { if(!isList()) throw std::runtime_error("Not a List"); return std::get<List>(data); }
    const Custom& asCustom() const { if(!isCustom()) throw std::runtime_error("Not a Custom type"); return std::get<Custom>(data); }
    const Reference& asReference() const { if(!isReference()) throw std::runtime_error("Not a Reference"); return std::get<Reference>(data); }
};