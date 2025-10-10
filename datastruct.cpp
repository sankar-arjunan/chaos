#include "datastruct.hpp"

void Object::add(const std::string& key, const Value& v) {
    auto it = std::lower_bound(fields.begin(), fields.end(), key,
        [](const std::pair<std::string, Value>& p, const std::string& k) { return p.first < k; });
    fields.insert(it, {key, v});
}

Value Object::toValue() const { return Value(*this); }

void List::add(const Value& v) { elements.push_back(v); }

Value List::toValue() const { return Value(*this); }

Value Custom::toValue() const { return Value(*this); }
