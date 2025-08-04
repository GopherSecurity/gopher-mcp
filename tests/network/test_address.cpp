#include <gtest/gtest.h>
#include <gmock/gmock.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/un.h>
#endif

#include "mcp/network/address_impl.h"

using namespace mcp;
using namespace mcp::network;
using namespace mcp::network::Address;
using namespace testing;

class AddressTest : public Test {
protected:
  void SetUp() override {}
  void TearDown() override {}
};

// ===== IPv4 Tests =====

TEST_F(AddressTest, IPv4Construction) {
  // From string
  Ipv4Instance addr1("192.168.1.1", 8080);
  EXPECT_EQ(addr1.addressAsString(), "192.168.1.1");
  EXPECT_EQ(addr1.port(), 8080);
  EXPECT_EQ(addr1.version(), IpVersion::v4);
  EXPECT_EQ(addr1.type(), Type::Ip);
  
  // From sockaddr_in
  sockaddr_in raw_addr;
  memset(&raw_addr, 0, sizeof(raw_addr));
  raw_addr.sin_family = AF_INET;
  raw_addr.sin_port = htons(443);
  inet_pton(AF_INET, "10.0.0.1", &raw_addr.sin_addr);
  
  Ipv4Instance addr2(&raw_addr);
  EXPECT_EQ(addr2.addressAsString(), "10.0.0.1");
  EXPECT_EQ(addr2.port(), 443);
}

TEST_F(AddressTest, IPv4InvalidAddress) {
  EXPECT_THROW(Ipv4Instance("256.256.256.256", 80), std::runtime_error);
  EXPECT_THROW(Ipv4Instance("not.an.ip.address", 80), std::runtime_error);
  EXPECT_THROW(Ipv4Instance("", 80), std::runtime_error);
}

TEST_F(AddressTest, IPv4StringRepresentation) {
  Ipv4Instance addr("127.0.0.1", 80);
  EXPECT_EQ(addr.asString(), "127.0.0.1:80");
  EXPECT_EQ(addr.asStringView(), "127.0.0.1:80");
  EXPECT_EQ(addr.addressAsString(), "127.0.0.1");
}

TEST_F(AddressTest, IPv4SpecialAddresses) {
  // Any address
  Ipv4Instance any("0.0.0.0", 0);
  EXPECT_TRUE(any.isAnyAddress());
  EXPECT_FALSE(any.isLoopbackAddress());
  EXPECT_FALSE(any.isMulticastAddress());
  
  // Loopback addresses
  Ipv4Instance loopback1("127.0.0.1", 0);
  EXPECT_FALSE(loopback1.isAnyAddress());
  EXPECT_TRUE(loopback1.isLoopbackAddress());
  EXPECT_FALSE(loopback1.isMulticastAddress());
  
  Ipv4Instance loopback2("127.255.255.255", 0);
  EXPECT_TRUE(loopback2.isLoopbackAddress());
  
  // Multicast addresses
  Ipv4Instance multicast1("224.0.0.1", 0);
  EXPECT_FALSE(multicast1.isAnyAddress());
  EXPECT_FALSE(multicast1.isLoopbackAddress());
  EXPECT_TRUE(multicast1.isMulticastAddress());
  
  Ipv4Instance multicast2("239.255.255.255", 0);
  EXPECT_TRUE(multicast2.isMulticastAddress());
  
  // Regular address
  Ipv4Instance regular("192.168.1.1", 0);
  EXPECT_FALSE(regular.isAnyAddress());
  EXPECT_FALSE(regular.isLoopbackAddress());
  EXPECT_FALSE(regular.isMulticastAddress());
}

TEST_F(AddressTest, IPv4Equality) {
  Ipv4Instance addr1("192.168.1.1", 80);
  Ipv4Instance addr2("192.168.1.1", 80);
  Ipv4Instance addr3("192.168.1.1", 81);
  Ipv4Instance addr4("192.168.1.2", 80);
  
  EXPECT_TRUE(addr1 == addr2);
  EXPECT_FALSE(addr1 == addr3);  // Different port
  EXPECT_FALSE(addr1 == addr4);  // Different address
  EXPECT_TRUE(addr1 != addr3);
}

TEST_F(AddressTest, IPv4RawAccess) {
  Ipv4Instance addr("192.168.1.1", 8080);
  
  // Get raw IPv4 address
  auto raw = addr.ipv4();
  ASSERT_TRUE(raw.has_value());
  
  // Convert back to verify
  char buffer[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &(*raw), buffer, sizeof(buffer));
  EXPECT_EQ(std::string(buffer), "192.168.1.1");
  
  // sockaddr access
  EXPECT_EQ(addr.sockAddr()->sa_family, AF_INET);
  EXPECT_EQ(addr.sockAddrLen(), sizeof(sockaddr_in));
}

// ===== IPv6 Tests =====

TEST_F(AddressTest, IPv6Construction) {
  // From string
  Ipv6Instance addr1("::1", 8080);
  EXPECT_EQ(addr1.addressAsString(), "::1");
  EXPECT_EQ(addr1.port(), 8080);
  EXPECT_EQ(addr1.version(), IpVersion::v6);
  EXPECT_EQ(addr1.type(), Type::Ip);
  
  // Full address
  Ipv6Instance addr2("2001:db8::1", 443);
  EXPECT_EQ(addr2.addressAsString(), "2001:db8::1");
  EXPECT_EQ(addr2.port(), 443);
  
  // From sockaddr_in6
  sockaddr_in6 raw_addr;
  memset(&raw_addr, 0, sizeof(raw_addr));
  raw_addr.sin6_family = AF_INET6;
  raw_addr.sin6_port = htons(80);
  inet_pton(AF_INET6, "fe80::1", &raw_addr.sin6_addr);
  
  Ipv6Instance addr3(&raw_addr);
  EXPECT_EQ(addr3.addressAsString(), "fe80::1");
  EXPECT_EQ(addr3.port(), 80);
}

TEST_F(AddressTest, IPv6InvalidAddress) {
  EXPECT_THROW(Ipv6Instance("not::valid::ipv6", 80), std::runtime_error);
  EXPECT_THROW(Ipv6Instance("gggg::1", 80), std::runtime_error);
  EXPECT_THROW(Ipv6Instance("", 80), std::runtime_error);
}

TEST_F(AddressTest, IPv6StringRepresentation) {
  // With brackets for port
  Ipv6Instance addr1("2001:db8::1", 80);
  EXPECT_EQ(addr1.asString(), "[2001:db8::1]:80");
  EXPECT_EQ(addr1.addressAsString(), "2001:db8::1");
  
  // Compressed zeros
  Ipv6Instance addr2("::1", 443);
  EXPECT_EQ(addr2.asString(), "[::1]:443");
}

TEST_F(AddressTest, IPv6SpecialAddresses) {
  // Any address
  Ipv6Instance any("::", 0);
  EXPECT_TRUE(any.isAnyAddress());
  EXPECT_FALSE(any.isLoopbackAddress());
  EXPECT_FALSE(any.isMulticastAddress());
  
  // Loopback
  Ipv6Instance loopback("::1", 0);
  EXPECT_FALSE(loopback.isAnyAddress());
  EXPECT_TRUE(loopback.isLoopbackAddress());
  EXPECT_FALSE(loopback.isMulticastAddress());
  
  // Multicast
  Ipv6Instance multicast1("ff02::1", 0);
  EXPECT_FALSE(multicast1.isAnyAddress());
  EXPECT_FALSE(multicast1.isLoopbackAddress());
  EXPECT_TRUE(multicast1.isMulticastAddress());
  
  Ipv6Instance multicast2("ff00::1", 0);
  EXPECT_TRUE(multicast2.isMulticastAddress());
  
  // Regular address
  Ipv6Instance regular("2001:db8::1", 0);
  EXPECT_FALSE(regular.isAnyAddress());
  EXPECT_FALSE(regular.isLoopbackAddress());
  EXPECT_FALSE(regular.isMulticastAddress());
}

TEST_F(AddressTest, IPv6Equality) {
  Ipv6Instance addr1("2001:db8::1", 80);
  Ipv6Instance addr2("2001:db8::1", 80);
  Ipv6Instance addr3("2001:db8::1", 81);
  Ipv6Instance addr4("2001:db8::2", 80);
  
  EXPECT_TRUE(addr1 == addr2);
  EXPECT_FALSE(addr1 == addr3);  // Different port
  EXPECT_FALSE(addr1 == addr4);  // Different address
}

TEST_F(AddressTest, IPv6RawAccess) {
  Ipv6Instance addr("2001:db8::1", 8080);
  
  // Get raw IPv6 address
  auto raw = addr.ipv6();
  ASSERT_TRUE(raw.has_value());
  EXPECT_EQ(raw->size(), 16);
  
  // Convert back to verify
  char buffer[INET6_ADDRSTRLEN];
  inet_ntop(AF_INET6, raw->data(), buffer, sizeof(buffer));
  EXPECT_EQ(std::string(buffer), "2001:db8::1");
  
  // sockaddr access
  EXPECT_EQ(addr.sockAddr()->sa_family, AF_INET6);
  EXPECT_EQ(addr.sockAddrLen(), sizeof(sockaddr_in6));
}

TEST_F(AddressTest, CrossVersionEquality) {
  Ipv4Instance v4("127.0.0.1", 80);
  Ipv6Instance v6("::1", 80);
  
  EXPECT_FALSE(v4 == v6);
  EXPECT_TRUE(v4 != v6);
}

// ===== Unix Domain Socket Tests (non-Windows) =====

#ifndef _WIN32
TEST_F(AddressTest, UnixSocketConstruction) {
  // Regular path
  PipeInstance addr1("/tmp/test.sock");
  EXPECT_EQ(addr1.path(), "/tmp/test.sock");
  EXPECT_EQ(addr1.asString(), "/tmp/test.sock");
  EXPECT_EQ(addr1.type(), Type::Pipe);
  
  // With mode
  PipeInstance addr2("/var/run/test.sock", 0600);
  EXPECT_EQ(addr2.path(), "/var/run/test.sock");
  EXPECT_EQ(addr2.mode(), 0600);
  
  // Abstract socket (Linux)
  std::string abstract_path = std::string(1, '\0') + "abstract_socket";
  PipeInstance addr3(abstract_path);
  EXPECT_EQ(addr3.path(), abstract_path);
  EXPECT_EQ(addr3.asString(), "@abstract_socket");  // @ prefix for display
}

TEST_F(AddressTest, UnixSocketFromSockaddr) {
  sockaddr_un raw_addr;
  memset(&raw_addr, 0, sizeof(raw_addr));
  raw_addr.sun_family = AF_UNIX;
  strcpy(raw_addr.sun_path, "/tmp/test.sock");
  
  socklen_t len = offsetof(sockaddr_un, sun_path) + strlen(raw_addr.sun_path) + 1;
  PipeInstance addr(&raw_addr, len);
  
  EXPECT_EQ(addr.path(), "/tmp/test.sock");
  EXPECT_EQ(addr.sockAddr()->sa_family, AF_UNIX);
}

TEST_F(AddressTest, UnixSocketPathTooLong) {
  std::string long_path(200, 'x');  // Exceeds sun_path size
  EXPECT_THROW(PipeInstance{long_path}, std::runtime_error);
}

TEST_F(AddressTest, UnixSocketEquality) {
  PipeInstance addr1("/tmp/test.sock");
  PipeInstance addr2("/tmp/test.sock");
  PipeInstance addr3("/tmp/other.sock");
  
  EXPECT_TRUE(addr1 == addr2);
  EXPECT_FALSE(addr1 == addr3);
}
#endif

// ===== Factory Function Tests =====

TEST_F(AddressTest, ParseInternetAddress) {
  // IPv4 with port
  auto addr1 = parseInternetAddress("192.168.1.1:80");
  ASSERT_NE(addr1, nullptr);
  EXPECT_EQ(addr1->ip()->addressAsString(), "192.168.1.1");
  EXPECT_EQ(addr1->ip()->port(), 80);
  
  // IPv4 without port (use default)
  auto addr2 = parseInternetAddress("10.0.0.1", 443);
  ASSERT_NE(addr2, nullptr);
  EXPECT_EQ(addr2->ip()->addressAsString(), "10.0.0.1");
  EXPECT_EQ(addr2->ip()->port(), 443);
  
  // IPv6 with port
  auto addr3 = parseInternetAddress("[2001:db8::1]:8080");
  ASSERT_NE(addr3, nullptr);
  EXPECT_EQ(addr3->ip()->addressAsString(), "2001:db8::1");
  EXPECT_EQ(addr3->ip()->port(), 8080);
  
  // IPv6 without port
  auto addr4 = parseInternetAddress("::1", 22);
  ASSERT_NE(addr4, nullptr);
  EXPECT_EQ(addr4->ip()->addressAsString(), "::1");
  EXPECT_EQ(addr4->ip()->port(), 22);
  
  // Invalid addresses
  EXPECT_EQ(parseInternetAddress("not.an.address"), nullptr);
  EXPECT_EQ(parseInternetAddress("256.256.256.256:80"), nullptr);
}

TEST_F(AddressTest, ParseInternetAddressNoPort) {
  // IPv4
  auto addr1 = parseInternetAddressNoPort("192.168.1.1", 8080);
  ASSERT_NE(addr1, nullptr);
  EXPECT_EQ(addr1->ip()->version(), IpVersion::v4);
  EXPECT_EQ(addr1->ip()->addressAsString(), "192.168.1.1");
  EXPECT_EQ(addr1->ip()->port(), 8080);
  
  // IPv6
  auto addr2 = parseInternetAddressNoPort("fe80::1", 443);
  ASSERT_NE(addr2, nullptr);
  EXPECT_EQ(addr2->ip()->version(), IpVersion::v6);
  EXPECT_EQ(addr2->ip()->addressAsString(), "fe80::1");
  EXPECT_EQ(addr2->ip()->port(), 443);
  
  // Invalid
  EXPECT_EQ(parseInternetAddressNoPort("invalid", 80), nullptr);
}

TEST_F(AddressTest, AddressFromSockAddr) {
  // IPv4
  sockaddr_storage storage;
  memset(&storage, 0, sizeof(storage));
  auto* addr4 = reinterpret_cast<sockaddr_in*>(&storage);
  addr4->sin_family = AF_INET;
  addr4->sin_port = htons(80);
  inet_pton(AF_INET, "127.0.0.1", &addr4->sin_addr);
  
  auto result1 = addressFromSockAddr(storage, sizeof(sockaddr_in));
  ASSERT_NE(result1, nullptr);
  EXPECT_EQ(result1->ip()->version(), IpVersion::v4);
  EXPECT_EQ(result1->ip()->port(), 80);
  
  // IPv6
  memset(&storage, 0, sizeof(storage));
  auto* addr6 = reinterpret_cast<sockaddr_in6*>(&storage);
  addr6->sin6_family = AF_INET6;
  addr6->sin6_port = htons(443);
  inet_pton(AF_INET6, "::1", &addr6->sin6_addr);
  
  auto result2 = addressFromSockAddr(storage, sizeof(sockaddr_in6));
  ASSERT_NE(result2, nullptr);
  EXPECT_EQ(result2->ip()->version(), IpVersion::v6);
  EXPECT_EQ(result2->ip()->port(), 443);
  
  // IPv4-mapped IPv6 (without v6only)
  memset(&storage, 0, sizeof(storage));
  addr6 = reinterpret_cast<sockaddr_in6*>(&storage);
  addr6->sin6_family = AF_INET6;
  addr6->sin6_port = htons(8080);
  // ::ffff:192.168.1.1
  inet_pton(AF_INET6, "::ffff:192.168.1.1", &addr6->sin6_addr);
  
  auto result3 = addressFromSockAddr(storage, sizeof(sockaddr_in6), false);
  ASSERT_NE(result3, nullptr);
  EXPECT_EQ(result3->ip()->version(), IpVersion::v4);  // Converted to v4
  EXPECT_EQ(result3->ip()->addressAsString(), "192.168.1.1");
  
  // Same with v6only=true
  auto result4 = addressFromSockAddr(storage, sizeof(sockaddr_in6), true);
  ASSERT_NE(result4, nullptr);
  EXPECT_EQ(result4->ip()->version(), IpVersion::v6);  // Stays v6
}

TEST_F(AddressTest, SpecialAddressFactories) {
  // Any address
  auto any4 = anyAddress(IpVersion::v4, 80);
  ASSERT_NE(any4, nullptr);
  EXPECT_TRUE(any4->ip()->isAnyAddress());
  EXPECT_EQ(any4->ip()->addressAsString(), "0.0.0.0");
  EXPECT_EQ(any4->ip()->port(), 80);
  
  auto any6 = anyAddress(IpVersion::v6, 443);
  ASSERT_NE(any6, nullptr);
  EXPECT_TRUE(any6->ip()->isAnyAddress());
  EXPECT_EQ(any6->ip()->addressAsString(), "::");
  EXPECT_EQ(any6->ip()->port(), 443);
  
  // Loopback address
  auto loop4 = loopbackAddress(IpVersion::v4, 8080);
  ASSERT_NE(loop4, nullptr);
  EXPECT_TRUE(loop4->ip()->isLoopbackAddress());
  EXPECT_EQ(loop4->ip()->addressAsString(), "127.0.0.1");
  EXPECT_EQ(loop4->ip()->port(), 8080);
  
  auto loop6 = loopbackAddress(IpVersion::v6, 22);
  ASSERT_NE(loop6, nullptr);
  EXPECT_TRUE(loop6->ip()->isLoopbackAddress());
  EXPECT_EQ(loop6->ip()->addressAsString(), "::1");
  EXPECT_EQ(loop6->ip()->port(), 22);
}

// ===== CIDR Range Tests =====

TEST_F(AddressTest, CidrRangeParsing) {
  // IPv4 CIDR
  auto range1 = CidrRange::parse("192.168.1.0/24");
  ASSERT_TRUE(range1.has_value());
  EXPECT_EQ(range1->address()->ip()->addressAsString(), "192.168.1.0");
  EXPECT_EQ(range1->prefixLength(), 24);
  EXPECT_EQ(range1->asString(), "192.168.1.0/24");
  
  // IPv6 CIDR
  auto range2 = CidrRange::parse("2001:db8::/32");
  ASSERT_TRUE(range2.has_value());
  EXPECT_EQ(range2->address()->ip()->addressAsString(), "2001:db8::");
  EXPECT_EQ(range2->prefixLength(), 32);
  
  // Invalid formats
  EXPECT_FALSE(CidrRange::parse("192.168.1.0").has_value());  // No prefix
  EXPECT_FALSE(CidrRange::parse("192.168.1.0/").has_value());  // Empty prefix
  EXPECT_FALSE(CidrRange::parse("192.168.1.0/33").has_value());  // Invalid prefix
  EXPECT_FALSE(CidrRange::parse("invalid/24").has_value());  // Invalid address
}

TEST_F(AddressTest, CidrRangeConstruction) {
  auto addr = parseInternetAddressNoPort("10.0.0.0");
  ASSERT_NE(addr, nullptr);
  
  CidrRange range(addr, 8);
  EXPECT_EQ(range.prefixLength(), 8);
  
  // Invalid prefix length
  EXPECT_THROW(CidrRange(addr, 33), std::invalid_argument);
  
  // Non-IP address
  auto pipe_addr = pipeAddress("/tmp/test");
  if (pipe_addr) {
    EXPECT_THROW(CidrRange(pipe_addr, 24), std::invalid_argument);
  }
}

TEST_F(AddressTest, CidrRangeContains) {
  // IPv4 /24 network
  auto range = CidrRange::parse("192.168.1.0/24");
  ASSERT_TRUE(range.has_value());
  
  auto addr1 = parseInternetAddressNoPort("192.168.1.1");
  auto addr2 = parseInternetAddressNoPort("192.168.1.255");
  auto addr3 = parseInternetAddressNoPort("192.168.2.1");
  auto addr4 = parseInternetAddressNoPort("10.0.0.1");
  
  EXPECT_TRUE(range->contains(*addr1));
  EXPECT_TRUE(range->contains(*addr2));
  EXPECT_FALSE(range->contains(*addr3));
  EXPECT_FALSE(range->contains(*addr4));
  
  // IPv4 /32 (single host)
  auto host_range = CidrRange::parse("192.168.1.1/32");
  ASSERT_TRUE(host_range.has_value());
  EXPECT_TRUE(host_range->contains(*addr1));
  EXPECT_FALSE(host_range->contains(*addr2));
  
  // IPv4 /0 (all addresses)
  auto all_range = CidrRange::parse("0.0.0.0/0");
  ASSERT_TRUE(all_range.has_value());
  EXPECT_TRUE(all_range->contains(*addr1));
  EXPECT_TRUE(all_range->contains(*addr3));
  EXPECT_TRUE(all_range->contains(*addr4));
}

TEST_F(AddressTest, CidrRangeContainsIPv6) {
  // IPv6 /64 network
  auto range = CidrRange::parse("2001:db8::/64");
  ASSERT_TRUE(range.has_value());
  
  auto addr1 = parseInternetAddressNoPort("2001:db8::1");
  auto addr2 = parseInternetAddressNoPort("2001:db8::ffff");
  auto addr3 = parseInternetAddressNoPort("2001:db9::1");
  auto addr4 = parseInternetAddressNoPort("fe80::1");
  
  EXPECT_TRUE(range->contains(*addr1));
  EXPECT_TRUE(range->contains(*addr2));
  EXPECT_FALSE(range->contains(*addr3));
  EXPECT_FALSE(range->contains(*addr4));
  
  // IPv6 /128 (single host)
  auto host_range = CidrRange::parse("2001:db8::1/128");
  ASSERT_TRUE(host_range.has_value());
  EXPECT_TRUE(host_range->contains(*addr1));
  EXPECT_FALSE(host_range->contains(*addr2));
}

TEST_F(AddressTest, CidrRangeNonAlignedBits) {
  // Test with non-byte-aligned prefix lengths
  auto range1 = CidrRange::parse("192.168.1.0/25");  // 25 bits
  ASSERT_TRUE(range1.has_value());
  
  auto addr1 = parseInternetAddressNoPort("192.168.1.127");  // In first half
  auto addr2 = parseInternetAddressNoPort("192.168.1.128");  // In second half
  
  EXPECT_TRUE(range1->contains(*addr1));
  EXPECT_FALSE(range1->contains(*addr2));
  
  // IPv6 with non-aligned bits
  auto range2 = CidrRange::parse("2001:db8::/65");
  ASSERT_TRUE(range2.has_value());
  
  auto addr3 = parseInternetAddressNoPort("2001:db8::7fff:ffff:ffff:ffff");
  auto addr4 = parseInternetAddressNoPort("2001:db8::8000:0:0:0");
  
  EXPECT_TRUE(range2->contains(*addr3));
  EXPECT_FALSE(range2->contains(*addr4));
}

TEST_F(AddressTest, CidrRangeCrossVersion) {
  // IPv4 range should not contain IPv6 addresses
  auto v4_range = CidrRange::parse("192.168.0.0/16");
  ASSERT_TRUE(v4_range.has_value());
  
  auto v6_addr = parseInternetAddressNoPort("::1");
  EXPECT_FALSE(v4_range->contains(*v6_addr));
  
  // IPv6 range should not contain IPv4 addresses
  auto v6_range = CidrRange::parse("2001:db8::/32");
  ASSERT_TRUE(v6_range.has_value());
  
  auto v4_addr = parseInternetAddressNoPort("192.168.1.1");
  EXPECT_FALSE(v6_range->contains(*v4_addr));
}

// Performance test for large number of address comparisons
TEST_F(AddressTest, AddressComparisonPerformance) {
  const int iterations = 100000;
  
  auto addr1 = parseInternetAddress("192.168.1.1:80");
  auto addr2 = parseInternetAddress("192.168.1.2:80");
  
  auto start = std::chrono::steady_clock::now();
  
  int equal_count = 0;
  for (int i = 0; i < iterations; ++i) {
    if (*addr1 == *addr2) {
      equal_count++;
    }
  }
  
  auto end = std::chrono::steady_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  
  // Should be fast - less than 1 microsecond per comparison
  EXPECT_LT(duration.count(), iterations);
  EXPECT_EQ(equal_count, 0);  // They should never be equal
}

// Test address polymorphism
TEST_F(AddressTest, AddressPolymorphism) {
  std::vector<InstanceConstSharedPtr> addresses;
  
  addresses.push_back(parseInternetAddress("192.168.1.1:80"));
  addresses.push_back(parseInternetAddress("[::1]:443"));
#ifndef _WIN32
  addresses.push_back(pipeAddress("/tmp/test.sock"));
#endif
  
  // All should have valid type and string representation
  for (const auto& addr : addresses) {
    ASSERT_NE(addr, nullptr);
    EXPECT_FALSE(addr->asString().empty());
    EXPECT_NE(addr->sockAddr(), nullptr);
    EXPECT_GT(addr->sockAddrLen(), 0);
  }
  
  // Type checking
  EXPECT_EQ(addresses[0]->type(), Type::Ip);
  EXPECT_EQ(addresses[1]->type(), Type::Ip);
#ifndef _WIN32
  if (addresses.size() > 2) {
    EXPECT_EQ(addresses[2]->type(), Type::Pipe);
  }
#endif
}