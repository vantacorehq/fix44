#include "fix44.hpp"

#include <cctype>

namespace fix44 {
namespace {

void reject_value(const std::string& v) {
    for (unsigned char c : v) {
        if (c < 0x20 || c > 0x7e) {
            throw Error(Err::Value, "value");
        }
    }
}

std::string checksum_field(unsigned sum) {
    sum %= 256;
    std::string s = "10=";
    s.push_back(static_cast<char>('0' + sum / 100));
    s.push_back(static_cast<char>('0' + (sum / 10) % 10));
    s.push_back(static_cast<char>('0' + sum % 10));
    s.push_back(SOH);
    return s;
}

std::vector<std::string> split(std::string_view wire) {
    if (wire.empty() || wire.back() != SOH) {
        throw Error(Err::Format, "message must end with SOH");
    }
    std::vector<std::string> out;
    std::string cur;
    for (char c : wire) {
        if (c == SOH) {
            if (cur.empty()) {
                throw Error(Err::Format, "empty field");
            }
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    return out;
}

Field parse_field(const std::string& raw) {
    auto eq = raw.find('=');
    if (eq == std::string::npos || eq == 0) {
        throw Error(Err::Format, "tag");
    }
    if (raw[0] == '0') {
        throw Error(Err::Format, "tag");
    }
    int tag = 0;
    for (size_t i = 0; i < eq; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(raw[i]))) {
            throw Error(Err::Format, "tag");
        }
        tag = tag * 10 + (raw[i] - '0');
        if (tag > 100000) {
            throw Error(Err::Format, "tag");
        }
    }
    if (tag == 0) {
        throw Error(Err::Format, "tag");
    }
    return Field{tag, raw.substr(eq + 1)};
}

bool positive_int(const std::string& s) {
    if (s.empty() || s.size() > 18 || s[0] == '0') {
        return false;
    }
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

bool price_text(const std::string& s) {
    if (s.empty() || s.size() > 32 || s.front() == '.' || s.back() == '.') {
        return false;
    }
    bool dot = false;
    bool nonzero = false;
    for (char c : s) {
        if (c == '.') {
            if (dot) {
                return false;
            }
            dot = true;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            if (c != '0') {
                nonzero = true;
            }
        } else {
            return false;
        }
    }
    if (!nonzero) {
        return false;
    }
    return !(s[0] == '0' && s.size() > 1 && s[1] != '.');
}

}  // namespace

std::string encode(const std::vector<Field>& body) {
    std::string payload;
    for (const auto& f : body) {
        if (f.tag == 8 || f.tag == 9 || f.tag == 10 || f.tag <= 0) {
            throw Error(Err::Format, "tag reserved or bad");
        }
        reject_value(f.value);
        payload += std::to_string(f.tag);
        payload.push_back('=');
        payload += f.value;
        payload.push_back(SOH);
    }
    std::string msg = "8=FIX.4.4";
    msg.push_back(SOH);
    msg += "9=";
    msg += std::to_string(payload.size());
    msg.push_back(SOH);
    msg += payload;
    unsigned sum = 0;
    for (unsigned char c : msg) {
        sum += c;
    }
    msg += checksum_field(sum);
    return msg;
}

std::vector<Field> decode(std::string_view wire) {
    auto raw = split(wire);
    if (raw.size() < 3) {
        throw Error(Err::Format, "too short");
    }
    std::vector<Field> fields;
    fields.reserve(raw.size());
    for (const auto& r : raw) {
        fields.push_back(parse_field(r));
    }
    if (fields.front().tag != 8 || fields.front().value != "FIX.4.4") {
        throw Error(Err::Format, "BeginString");
    }
    if (fields[1].tag != 9) {
        throw Error(Err::Format, "BodyLength");
    }
    if (fields.back().tag != 10 || fields.back().value.size() != 3) {
        throw Error(Err::Format, "CheckSum");
    }
    auto last = wire.rfind(SOH, wire.size() - 2);
    if (last == std::string_view::npos) {
        throw Error(Err::Format, "tail");
    }
    size_t check_at = last + 1;
    unsigned sum = 0;
    for (size_t i = 0; i < check_at; ++i) {
        sum += static_cast<unsigned char>(wire[i]);
    }
    sum %= 256;
    unsigned got = 0;
    for (char c : fields.back().value) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw Error(Err::Checksum, "digits");
        }
        got = got * 10u + static_cast<unsigned>(c - '0');
    }
    if (got != sum) {
        throw Error(Err::Checksum, "mismatch");
    }
    auto second = wire.find(SOH);
    auto third = wire.find(SOH, second + 1);
    if (second == std::string_view::npos || third == std::string_view::npos) {
        throw Error(Err::Format, "header");
    }
    size_t body = check_at - (third + 1);
    if (fields[1].value != std::to_string(body)) {
        throw Error(Err::Length, "mismatch");
    }
    return fields;
}

const std::string* find(const std::vector<Field>& fields, int tag) {
    for (const auto& f : fields) {
        if (f.tag == tag) {
            return &f.value;
        }
    }
    return nullptr;
}

void require_header(const std::vector<Field>& fields) {
    auto need = [&](int tag) {
        auto* v = find(fields, tag);
        if (v == nullptr || v->empty()) {
            throw Error(Err::Required, std::to_string(tag));
        }
        return v;
    };
    need(35);
    need(49);
    need(56);
    if (!positive_int(*need(34))) {
        throw Error(Err::Value, "MsgSeqNum");
    }
    need(52);
}

void require_new_order(const std::vector<Field>& fields) {
    require_header(fields);
    auto* mt = find(fields, 35);
    if (mt == nullptr || *mt != "D") {
        throw Error(Err::Value, "MsgType");
    }
    auto need = [&](int tag) {
        auto* v = find(fields, tag);
        if (v == nullptr || v->empty()) {
            throw Error(Err::Required, std::to_string(tag));
        }
        return *v;
    };
    auto id = need(11);
    if (id.size() > 64) {
        throw Error(Err::Value, "ClOrdID");
    }
    auto sym = need(55);
    if (sym.size() > 32) {
        throw Error(Err::Value, "Symbol");
    }
    for (char c : sym) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-')) {
            throw Error(Err::Value, "Symbol");
        }
    }
    auto side = need(54);
    if (side != "1" && side != "2") {
        throw Error(Err::Value, "Side");
    }
    if (!positive_int(need(38))) {
        throw Error(Err::Value, "OrderQty");
    }
    auto typ = need(40);
    if (typ == "2") {
        if (!price_text(need(44))) {
            throw Error(Err::Value, "Price");
        }
    } else if (typ == "1") {
        if (find(fields, 44) != nullptr) {
            throw Error(Err::Value, "market with price");
        }
    } else {
        throw Error(Err::Value, "OrdType");
    }
}

std::vector<Party> parties(const std::vector<Field>& fields) {
    std::vector<Party> out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].tag != 453) {
            continue;
        }
        if (fields[i].value == "0") {
            return out;
        }
        if (!positive_int(fields[i].value)) {
            throw Error(Err::Value, "NoPartyIDs");
        }
        long long n = 0;
        for (char ch : fields[i].value) {
            n = n * 10 + (ch - '0');
        }
        if (n > 1024) {
            throw Error(Err::Value, "NoPartyIDs");
        }
        if (i + 1 > fields.size() ||
            static_cast<size_t>(n) > (fields.size() - (i + 1)) / 3) {
            throw Error(Err::Format, "party group short");
        }
        for (int k = 0; k < n; ++k) {
            const auto& a = fields[i + 1 + static_cast<size_t>(k) * 3];
            const auto& b = fields[i + 2 + static_cast<size_t>(k) * 3];
            const auto& c = fields[i + 3 + static_cast<size_t>(k) * 3];
            if (a.tag != 448 || b.tag != 447 || c.tag != 452) {
                throw Error(Err::Format, "party group");
            }
            if (a.value.empty() || b.value.size() != 1 || !positive_int(c.value)) {
                throw Error(Err::Value, "party");
            }
            long long role = 0;
            for (char ch : c.value) {
                role = role * 10 + (ch - '0');
            }
            if (role > 2147483647LL) {
                throw Error(Err::Value, "party");
            }
            out.push_back(Party{a.value, b.value[0], static_cast<int>(role)});
        }
        return out;
    }
    return out;
}

}  // namespace fix44
