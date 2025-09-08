#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <iomanip>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;
using json = nlohmann::json;

// Order book structures
using PriceLevel = std::pair<std::string, std::string>; // price, quantity
std::map<std::string, std::string, std::greater<std::string>> bids; // Descending order for bids
std::map<std::string, std::string> asks; // Ascending order for asks

long long last_update_id = 0;

// Function to fetch order book snapshot via REST API
void fetch_snapshot(const std::string& symbol, int limit) {
    try {
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        tcp::resolver resolver{ioc};
        beast::ssl_stream<tcp::socket> stream{ioc, ctx};

        // Set SNI Hostname
        if (!SSL_set_tlsext_host_name(stream.native_handle(), "api.binance.com")) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        auto const results = resolver.resolve("api.binance.com", "443");
        net::connect(stream.next_layer(), results.begin(), results.end());
        stream.handshake(ssl::stream_base::client);

        http::request<http::string_body> req{http::verb::get, "/api/v3/depth?symbol=" + symbol + "&limit=" + std::to_string(limit), 11};
        req.set(http::field::host, "api.binance.com");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);

        std::string body = beast::buffers_to_string(res.body().data());
        json data = json::parse(body);

        last_update_id = data["lastUpdateId"].get<long long>();

        bids.clear();
        asks.clear();

        for (const auto& bid : data["bids"]) {
            bids[bid[0].get<std::string>()] = bid[1].get<std::string>();
        }

        for (const auto& ask : data["asks"]) {
            asks[ask[0].get<std::string>()] = ask[1].get<std::string>();
        }

        std::cout << "Fetched snapshot with lastUpdateId: " << last_update_id << std::endl;

        beast::error_code ec;
        stream.shutdown(ec);
    } catch (std::exception const& e) {
        std::cerr << "Error fetching snapshot: " << e.what() << std::endl;
    }
}

// Function to apply depth update
void apply_update(const json& update) {
    long long event_U = update["U"].get<long long>();
    long long event_u = update["u"].get<long long>();
    long long event_pu = update.contains("pu") ? update["pu"].get<long long>() : -1; // For checking continuity, if available

    // Check if this update is after the snapshot
    if (event_u <= last_update_id) {
        return; // Old update, discard
    }

    // For the first update after snapshot
    if (last_update_id > 0 && event_U > last_update_id + 1) {
        std::cerr << "Out of sync, need to refetch snapshot" << std::endl;
        // In production, refetch snapshot and restart
        return;
    }

    // Apply bids
    for (const auto& b : update["b"]) {
        std::string price = b[0].get<std::string>();
        std::string qty = b[1].get<std::string>();
        if (qty == "0") {
            bids.erase(price);
        } else {
            bids[price] = qty;
        }
    }

    // Apply asks
    for (const auto& a : update["a"]) {
        std::string price = a[0].get<std::string>();
        std::string qty = a[1].get<std::string>();
        if (qty == "0") {
            asks.erase(price);
        } else {
            asks[price] = qty;
        }
    }

    last_update_id = event_u;

    // In production, check if event_pu == previous u for continuity
}

// Function to print order book (for demo)
void print_order_book(int depth = 10) {
    std::cout << std::fixed << std::setprecision(8);
    std::cout << "Asks:" << std::endl;
    int count = 0;
    for (auto it = asks.begin(); it != asks.end() && count < depth; ++it, ++count) {
        std::cout << it->first << " : " << it->second << std::endl;
    }

    std::cout << "Bids:" << std::endl;
    count = 0;
    for (auto it = bids.begin(); it != bids.end() && count < depth; ++it, ++count) {
        std::cout << it->first << " : " << it->second << std::endl;
    }
    std::cout << "------------------------" << std::endl;
}

int main() {
    std::string symbol = "btcusdt";
    int limit = 1000; // Depth for snapshot

    // Step 1: Fetch initial snapshot
    fetch_snapshot(symbol, limit);

    try {
        // Step 2: Connect to WebSocket
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        tcp::resolver resolver{ioc};
        websocket::stream<beast::ssl_stream<tcp::socket>> ws{ioc, ctx};

        auto const results = resolver.resolve("stream.binance.com", "9443");
        auto ep = net::connect(get_lowest_layer(ws), results);

        // Set SNI Hostname
        if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), "stream.binance.com")) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        ws.next_layer().handshake(ssl::stream_base::client);

        ws.set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(http::field::host, "stream.binance.com");
                req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            }));

        ws.handshake("stream.binance.com:9443", "/ws/" + std::string(symbol.begin(), symbol.end()) + "@depth@100ms");

        // Buffer for reading
        beast::flat_buffer buffer;

        // Subscribe if needed, but since we used /ws/<stream>, it's already subscribed

        // Receive updates in a loop
        while (true) {
            ws.read(buffer);
            std::string message = beast::buffers_to_string(buffer.data());
            buffer.consume(buffer.size());

            json data = json::parse(message);

            if (data["e"] == "depthUpdate") {
                apply_update(data);
                // For demo, print every few updates
                static int update_count = 0;
                if (++update_count % 10 == 0) {
                    print_order_book(20); // Print top 10 levels
                }
            }
        }

        ws.close(websocket::close_code::normal);
    } catch (std::exception const& e) {
        std::cerr << "WebSocket error: " << e.what() << std::endl;
    }

    return 0;
}
