#include "check.hpp"

#include "fastsm/fastsm.hpp"
#include "fastsm/net/http_client.hpp"

#include <cstring>

// M0 placeholder tests: prove the core links and a basic contract works.
// Real coverage (HTML stripping, date parsing, DTO mapping) arrives in M1.

static void test_version() {
    CHECK(fastsm::version() != nullptr);
    CHECK(std::strlen(fastsm::version()) > 0);
}

static void test_http_header_lookup() {
    fastsm::net::HttpResponse res;
    res.status = 200;
    res.headers = {{"Content-Type", "application/json"}, {"Link", "<x>; rel=\"next\""}};

    CHECK(res.ok());
    // Case-insensitive lookup.
    CHECK(res.header("content-type").has_value());
    CHECK_EQ(res.header("content-type").value(), std::string("application/json"));
    CHECK(!res.header("missing").has_value());
}

int main() {
    test_version();
    test_http_header_lookup();

    std::printf("%d checks, %d failures\n", fastsmtest::checks(), fastsmtest::failures());
    return fastsmtest::failures() == 0 ? 0 : 1;
}
