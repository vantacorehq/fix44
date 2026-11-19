#include "fix44.hpp"

#include <cstdio>
#include <iostream>
#include <string>

static int checks;

static void check(bool ok, const char* msg) {
    if (!ok) {
        std::cerr << "FAIL " << msg << "\n";
        std::exit(1);
    }
    ++checks;
}

template <typename F>
static void fails(fix44::Err code, const char* msg, F&& fn) {
    try {
        fn();
    } catch (const fix44::Error& e) {
        check(e.code == code, msg);
        return;
    }
    std::cerr << "FAIL expected " << msg << "\n";
    std::exit(1);
}

static int manual_sum(const std::string& s) {
    int sum = 0;
    for (unsigned char c : s) {
        sum += c;
    }
    return sum % 256;
}

int main() {
    const std::string tiny = std::string("8=FIX.4.4\x01") + "9=5\x01" + "35=0\x01";
    int sum = manual_sum(tiny);
    char ck[16];
    std::snprintf(ck, sizeof ck, "10=%03d\x01", sum);
    auto fields = fix44::decode(tiny + ck);
    check(fields.size() == 4, "tiny size");
    check(fields[2].tag == 35 && fields[2].value == "0", "heartbeat type");
    fails(fix44::Err::Checksum, "bad sum", [&] { fix44::decode(tiny + "10=000\x01"); });
    fails(fix44::Err::Length, "bad len", [&] {
        std::string bad = tiny;
        auto p = bad.find("9=5");
        bad[p + 2] = '4';
        int s2 = manual_sum(bad);
        char c2[16];
        std::snprintf(c2, sizeof c2, "10=%03d\x01", s2);
        fix44::decode(bad + c2);
    });

    std::vector<fix44::Field> body = {
        {35, "D"}, {49, "SENDER"}, {56, "TARGET"}, {34, "1"}, {52, "20261119-18:40:00"},
        {11, "ORD-1"}, {55, "AAPL"}, {54, "1"}, {38, "100"}, {40, "2"}, {44, "150.25"},
        {453, "2"}, {448, "P1"}, {447, "D"}, {452, "1"}, {448, "P2"}, {447, "D"}, {452, "3"},
    };
    auto wire = fix44::encode(body);
    auto back = fix44::decode(wire);
    check(back.size() == body.size() + 3, "wrapped");
    check(back[0].value == "FIX.4.4", "begin");
    for (size_t i = 0; i < body.size(); ++i) {
        check(back[i + 2] == body[i], "field order");
    }
    fix44::require_new_order(back);
    auto ps = fix44::parties(back);
    check(ps.size() == 2 && ps[0].id == "P1" && ps[0].role == 1 && ps[1].role == 3, "parties");

    body[9] = {40, "1"};
    body.erase(body.begin() + 10);
    auto mkt = fix44::decode(fix44::encode(body));
    fix44::require_new_order(mkt);
    fails(fix44::Err::Value, "market price", [&] {
        auto extra = body;
        extra.insert(extra.begin() + 10, {44, "1"});
        fix44::require_new_order(fix44::decode(fix44::encode(extra)));
    });
    fails(fix44::Err::Required, "no qty", [&] {
        auto bad = body;
        bad.erase(bad.begin() + 8);
        fix44::require_new_order(fix44::decode(fix44::encode(bad)));
    });
    fails(fix44::Err::Value, "bad side", [&] {
        auto bad = body;
        bad[7] = {54, "3"};
        fix44::require_new_order(fix44::decode(fix44::encode(bad)));
    });
    fails(fix44::Err::Format, "short group", [&] {
        auto bad = body;
        bad.insert(bad.begin() + 10, {453, "2"});
        fix44::parties(fix44::decode(fix44::encode(bad)));
    });
    fails(fix44::Err::Value, "soh", [&] { fix44::encode({{35, std::string("A") + fix44::SOH}}); });
    fails(fix44::Err::Format, "no soh tail", [&] { fix44::decode("8=FIX.4.4"); });

    fails(fix44::Err::Value, "zero price", [&] {
        fix44::require_new_order(fix44::decode(fix44::encode({
            {35, "D"}, {49, "S"}, {56, "T"}, {34, "1"}, {52, "t"},
            {11, "A"}, {55, "AAPL"}, {54, "1"}, {38, "1"}, {40, "2"}, {44, "0.00"},
        })));
    });
    fails(fix44::Err::Value, "lead price", [&] {
        fix44::require_new_order(fix44::decode(fix44::encode({
            {35, "D"}, {49, "S"}, {56, "T"}, {34, "1"}, {52, "t"},
            {11, "A"}, {55, "AAPL"}, {54, "1"}, {38, "1"}, {40, "2"}, {44, "01"},
        })));
    });
    fails(fix44::Err::Value, "dot price", [&] {
        fix44::require_new_order(fix44::decode(fix44::encode({
            {35, "D"}, {49, "S"}, {56, "T"}, {34, "1"}, {52, "t"},
            {11, "A"}, {55, "AAPL"}, {54, "1"}, {38, "1"}, {40, "2"}, {44, ".5"},
        })));
    });
    fails(fix44::Err::Value, "binary", [&] { fix44::encode({{35, std::string(1, '\n')}}); });
    fails(fix44::Err::Format, "reserved", [&] { fix44::encode({{8, "FIX.4.4"}}); });
    auto hb = fix44::decode(fix44::encode({{35, "0"}, {49, "S"}, {56, "T"}, {34, "7"}, {52, "t"}}));
    check(fix44::parties(hb).empty(), "no parties");
    fix44::require_header(hb);
    auto zero = hb;
    zero.push_back({453, "0"});
    check(fix44::parties(zero).empty(), "zero parties");

    std::string lead = std::string("8=FIX.4.4\x01") + "9=5\x01" + "01=X\x01";
    char lck[16];
    std::snprintf(lck, sizeof lck, "10=%03d\x01", manual_sum(lead));
    fails(fix44::Err::Format, "lead tag", [&] { fix44::decode(lead + lck); });

    std::cout << "fix44: " << checks << " checks passed\n";
    return 0;
}
