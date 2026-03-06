#include <chrono>
#include <cstdint>

#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/delegating_route_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"

#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Router {
namespace {

class FakeClusterSpecifierPluginFactoryConfig : public ClusterSpecifierPluginFactoryConfig {
public:
  class FakeClusterSpecifierPlugin : public ClusterSpecifierPlugin {
  public:
    FakeClusterSpecifierPlugin(absl::string_view cluster) : cluster_name_(cluster) {}

    RouteConstSharedPtr route(RouteEntryAndRouteConstSharedPtr parent,
                              const Http::RequestHeaderMap&, const StreamInfo::StreamInfo&,
                              uint64_t) const override {
      ASSERT(dynamic_cast<const RouteEntryImplBase*>(parent.get()) != nullptr);
      return std::make_shared<Router::DynamicRouteEntry>(parent, std::string(cluster_name_));
    }

    const std::string cluster_name_;
  };

  FakeClusterSpecifierPluginFactoryConfig() = default;
  ClusterSpecifierPluginSharedPtr
  createClusterSpecifierPlugin(const Protobuf::Message& config,
                               Server::Configuration::ServerFactoryContext&) override {
    const auto& typed_config = dynamic_cast<const Protobuf::Struct&>(config);
    return std::make_shared<FakeClusterSpecifierPlugin>(
        typed_config.fields().at("name").string_value());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<Protobuf::Struct>();
  }

  std::string name() const override { return "envoy.router.cluster_specifier_plugin.fake"; }
};

class ConfigImplIntegrationTest : public Envoy::HttpIntegrationTest, public testing::Test {
public:
  ConfigImplIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4) {}

  void initializeRoute(const std::string& vhost_config_yaml) {
    envoy::config::route::v3::VirtualHost vhost;
    TestUtility::loadFromYaml(vhost_config_yaml, vhost);
    config_helper_.addVirtualHost(vhost);
    initialize();
  }
};

static const std::string ClusterSpecifierPluginUnknownCluster =
    R"EOF(
name: test_cluster_specifier_plugin
domains:
- cluster.specifier.plugin
routes:
- name: test_route_1
  match:
    prefix: /test/route/1
  route:
    inline_cluster_specifier_plugin:
      extension:
        name: fake
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            name: cluster_0
- name: test_route_2
  match:
    prefix: /test/route/2
  route:
    inline_cluster_specifier_plugin:
      extension:
        name: fake
        typed_config:
          "@type": type.googleapis.com/google.protobuf.Struct
          value:
            name: cluster_unknown
)EOF";

TEST_F(ConfigImplIntegrationTest, ClusterSpecifierPluginTest) {
  FakeClusterSpecifierPluginFactoryConfig factory;
  Registry::InjectFactory<ClusterSpecifierPluginFactoryConfig> registered(factory);

  initializeRoute(ClusterSpecifierPluginUnknownCluster);

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestResponseHeaderMapImpl response_headers{
        {"server", "envoy"},
        {":status", "200"},
    };

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test/route/1"},
                                                   {":scheme", "http"},
                                                   {":authority", "cluster.specifier.plugin"}};

    auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ(response->headers().getStatusValue(), "200");

    cleanupUpstreamAndDownstream();
  }

  {
    codec_client_ = makeHttpConnection(lookupPort("http"));

    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/test/route/2"},
                                                   {":scheme", "http"},
                                                   {":authority", "cluster.specifier.plugin"}};

    // Second route will be selected and unknown cluster name will be return by the cluster
    // specifier plugin.
    auto response = codec_client_->makeHeaderOnlyRequest(request_headers);
    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());
    EXPECT_THAT(response->headers(), Http::HttpStatusIs("503"));

    cleanupUpstreamAndDownstream();
  }
}

// Integration test for weighted cluster hash policy.
// Both clusters share the same upstream endpoint (cluster_1 is added via MergeFrom(cluster_0)).
// Traffic distribution is measured via per-cluster Envoy stats rather than physical upstream
// tracking, which would not work when both clusters resolve to the same fake upstream.
class WeightedClusterHashPolicyIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  WeightedClusterHashPolicyIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initializeConfig() {
    // Add cluster_1 configuration. MergeFrom(cluster_0) copies its endpoint address, so both
    // clusters connect to the same fake upstream — which is fine since we measure distribution
    // via cluster stats (cluster.cluster_N.upstream_rq_total), not physical upstream connections.
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      cluster->set_name("cluster_1");
      cluster->mutable_load_assignment()->set_cluster_name("cluster_1");
    });

    // Configure the route with weighted clusters and hash policy
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          hcm.mutable_route_config()->set_name("test_weighted_cluster_hash_policy");

          auto* vhost = hcm.mutable_route_config()->add_virtual_hosts();
          vhost->set_name("test_weighted_cluster_hash_policy");
          vhost->add_domains("weighted.cluster.hash.test");

          auto* route = vhost->add_routes();
          route->mutable_match()->set_prefix("/hash-test");

          auto* weighted_clusters = route->mutable_route()->mutable_weighted_clusters();

          auto* cluster0 = weighted_clusters->add_clusters();
          cluster0->set_name("cluster_0");
          cluster0->mutable_weight()->set_value(60);

          auto* cluster1 = weighted_clusters->add_clusters();
          cluster1->set_name("cluster_1");
          cluster1->mutable_weight()->set_value(40);

          // Enable hash policy for weighted clusters
          weighted_clusters->mutable_use_hash_policy()->set_value(true);

          auto* hash_policy = route->mutable_route()->add_hash_policy();
          hash_policy->mutable_header()->set_header_name("x-user-id");
        });

    HttpIntegrationTest::initialize();
  }

  // Send one request and wait for a 200 response. Both clusters share fake_upstreams_[0].
  void sendOneRequest(const std::string& user_id) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                   {":path", "/hash-test"},
                                                   {":scheme", "http"},
                                                   {":authority", "weighted.cluster.hash.test"},
                                                   {"x-user-id", user_id}};
    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"}};
    sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);
    cleanupUpstreamAndDownstream();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, WeightedClusterHashPolicyIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(WeightedClusterHashPolicyIntegrationTest, SameUserIdGoesToSameUpstream) {
  initializeConfig();

  // With hash policy enabled, the same user ID always hashes to the same cluster.
  // After N requests with the same user ID, exactly one cluster counter should equal N.
  const int num_requests = 5;
  for (int i = 0; i < num_requests; ++i) {
    sendOneRequest("consistent-user-123");
  }

  const uint64_t cluster_0_rq =
      test_server_->counter("cluster.cluster_0.upstream_rq_total")->value();
  const uint64_t cluster_1_rq =
      test_server_->counter("cluster.cluster_1.upstream_rq_total")->value();

  EXPECT_EQ(cluster_0_rq + cluster_1_rq, static_cast<uint64_t>(num_requests));
  // One cluster should have all requests, the other none (hash is deterministic per user).
  EXPECT_TRUE(cluster_0_rq == static_cast<uint64_t>(num_requests) ||
              cluster_1_rq == static_cast<uint64_t>(num_requests))
      << "cluster_0=" << cluster_0_rq << " cluster_1=" << cluster_1_rq
      << ": same user ID should consistently route to the same cluster";
}

TEST_P(WeightedClusterHashPolicyIntegrationTest, DifferentUserIdsCanGoToDifferentClusters) {
  initializeConfig();

  // Send requests for 20 different user IDs. With hash-based selection across 2 clusters
  // (60/40 weights), we expect both clusters to be used. Measure via cluster stats.
  const int num_requests = 20;
  for (int i = 0; i < num_requests; ++i) {
    sendOneRequest("user-" + std::to_string(i));
  }

  const uint64_t cluster_0_rq =
      test_server_->counter("cluster.cluster_0.upstream_rq_total")->value();
  const uint64_t cluster_1_rq =
      test_server_->counter("cluster.cluster_1.upstream_rq_total")->value();

  EXPECT_EQ(cluster_0_rq + cluster_1_rq, static_cast<uint64_t>(num_requests));
  // With 20 different user IDs, at least one request should go to each cluster.
  EXPECT_GE(cluster_0_rq, 1U) << "cluster_0 should have received at least one request";
  EXPECT_GE(cluster_1_rq, 1U) << "cluster_1 should have received at least one request";
}

TEST_P(WeightedClusterHashPolicyIntegrationTest, WeightedDistributionTest) {
  initializeConfig();

  // Send 100 requests with distinct user IDs. With use_hash_policy enabled and weights of
  // 60/40, the hash-based selection should distribute traffic roughly according to those weights.
  // Distribution is measured via Envoy's per-cluster request counters.
  const int num_requests = 100;
  for (int i = 0; i < num_requests; ++i) {
    sendOneRequest("user-" + std::to_string(i));
  }

  const uint64_t cluster_0_rq =
      test_server_->counter("cluster.cluster_0.upstream_rq_total")->value();
  const uint64_t cluster_1_rq =
      test_server_->counter("cluster.cluster_1.upstream_rq_total")->value();

  ASSERT_EQ(cluster_0_rq + cluster_1_rq, static_cast<uint64_t>(num_requests));

  const double ratio_0 = static_cast<double>(cluster_0_rq) / num_requests;
  const double ratio_1 = static_cast<double>(cluster_1_rq) / num_requests;

  // Allow ±20% variance around the configured 60/40 weights.
  EXPECT_GE(ratio_0, 0.4) << "cluster_0 should get at least 40% (got " << cluster_0_rq << ")";
  EXPECT_LE(ratio_0, 0.8) << "cluster_0 should get at most 80% (got " << cluster_0_rq << ")";
  EXPECT_GE(ratio_1, 0.2) << "cluster_1 should get at least 20% (got " << cluster_1_rq << ")";
  EXPECT_LE(ratio_1, 0.6) << "cluster_1 should get at most 60% (got " << cluster_1_rq << ")";
}

} // namespace
} // namespace Router
} // namespace Envoy
