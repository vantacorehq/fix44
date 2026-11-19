#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fix44 {

constexpr char SOH = '\x01';

enum class Err { Format, Length, Checksum, Required, Value };

class Error : public std::runtime_error {
public:
    Err code;
    Error(Err c, const std::string& m) : std::runtime_error(m), code(c) {}
};

struct Field {
    int tag;
    std::string value;
    bool operator==(const Field& o) const { return tag == o.tag && value == o.value; }
};

struct Party {
    std::string id;
    char source;
    int role;
    bool operator==(const Party& o) const {
        return id == o.id && source == o.source && role == o.role;
    }
};

// Body fields only. 8, 9 and 10 are added here. BeginString is FIX.4.4.
std::string encode(const std::vector<Field>& body);

// Full wire message, including 8, 9 and 10. Checks length and checksum.
std::vector<Field> decode(std::string_view wire);

// 35, 49, 56, 34, 52 must be present. 34 is a positive integer. 52 is non-empty.
void require_header(const std::vector<Field>& fields);

// NewOrderSingle (35=D): ClOrdID, Symbol, Side, OrderQty, OrdType.
// Limit (40=2) needs Price. Market (40=1) must not carry Price.
void require_new_order(const std::vector<Field>& fields);

// NoPartyIDs (453) followed by repeating 448, 447, 452. Absent group is empty.
std::vector<Party> parties(const std::vector<Field>& fields);

const std::string* find(const std::vector<Field>& fields, int tag);

}  // namespace fix44
