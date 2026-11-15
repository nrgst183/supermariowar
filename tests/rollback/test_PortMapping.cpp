#include "doctest.h"

#include "PortMapping.h"

using namespace rollback::upnp::detail;

TEST_CASE("UPnP: URLs")
{
    Url u;
    REQUIRE(parseUrl("http://192.168.1.1:49152/rootDesc.xml", u));
    CHECK(u.host == "192.168.1.1");
    CHECK(u.port == 49152);
    CHECK(u.path == "/rootDesc.xml");
    REQUIRE(parseUrl("HTTP://router.lan", u));
    CHECK(u.host == "router.lan");
    CHECK(u.port == 80);
    CHECK(u.path == "/");
    CHECK_FALSE(parseUrl("https://192.168.1.1/x", u));
    CHECK_FALSE(parseUrl("http://192.168.1.1:99999/x", u));
}

TEST_CASE("UPnP: the location in an SSDP reply")
{
    CHECK(ssdpLocation("HTTP/1.1 200 OK\r\nST: x\r\nLOCATION: http://10.0.0.1:5000/desc.xml\r\n\r\n") == "http://10.0.0.1:5000/desc.xml");
    CHECK(ssdpLocation("HTTP/1.1 200 OK\r\nlocation:http://10.0.0.1/d\r\n") == "http://10.0.0.1/d");
    CHECK(ssdpLocation("HTTP/1.1 200 OK\r\nST: x\r\n\r\n").empty());
}

TEST_CASE("UPnP: the WAN services of a router, absolute and IP first")
{
    const std::string desc =
        "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><device><serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType><controlURL>/l3f</controlURL></service>"
        "</serviceList><deviceList><device><serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:WANPPPConnection:1</serviceType><controlURL>ppp</controlURL></service>"
        "<service>\n  <serviceType>urn:schemas-upnp-org:service:WANIPConnection:2</serviceType>\n  <controlURL>/ctl/IPConn</controlURL>\n</service>"
        "</serviceList></device></deviceList></device></root>";
    const auto services = wanServices(desc, "http://192.168.0.1:1900/igd/desc.xml");
    REQUIRE(services.size() == 2);
    CHECK(services[0].type == "urn:schemas-upnp-org:service:WANIPConnection:2");
    CHECK(services[0].controlUrl == "http://192.168.0.1:1900/ctl/IPConn");
    CHECK(services[1].controlUrl == "http://192.168.0.1:1900/igd/ppp");

    //(URLBase, and absolute control URLs)
    const std::string withBase =
        "<root><URLBase>http://10.1.1.1:2000</URLBase><device><serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>/a</controlURL></service>"
        "<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType><controlURL>http://10.1.1.1:3000/b</controlURL></service>"
        "</serviceList></device></root>";
    const auto based = wanServices(withBase, "http://10.1.1.1:1900/d.xml");
    REQUIRE(based.size() == 2);
    CHECK(based[0].controlUrl == "http://10.1.1.1:2000/a");
    CHECK(based[1].controlUrl == "http://10.1.1.1:3000/b");
}

TEST_CASE("UPnP: XML values with namespace prefixes")
{
    const std::string fault =
        "<s:Envelope><s:Body><s:Fault><detail><UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
        "<errorCode>718</errorCode><errorDescription>ConflictInMappingEntry</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>";
    CHECK(xmlValue(fault, "errorCode") == "718");
    CHECK(xmlValue("<u:R><NewExternalIPAddress>203.0.113.5</NewExternalIPAddress></u:R>", "NewExternalIPAddress") == "203.0.113.5");
    CHECK(xmlValue("<a><m:NewExternalIPAddress> 1.2.3.4 </m:NewExternalIPAddress></a>", "NewExternalIPAddress") == "1.2.3.4");
    CHECK(xmlValue("<a><b/></a>", "b").empty());
    CHECK(xmlValue("<a><bc>x</bc></a>", "b").empty());
}

TEST_CASE("UPnP: HTTP bodies, plain and chunked")
{
    int status = 0;
    CHECK(httpBody("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", status) == "hello");
    CHECK(status == 200);
    CHECK(httpBody("HTTP/1.1 500 Internal Server Error\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n", status) == "hello world");
    CHECK(status == 500);
    httpBody("garbage", status);
    CHECK(status == 0);
}

TEST_CASE("UPnP: SOAP request")
{
    const std::string body = soapEnvelope("urn:schemas-upnp-org:service:WANIPConnection:1", "DeletePortMapping",
        {{"NewRemoteHost", ""}, {"NewExternalPort", "7300"}, {"NewProtocol", "UDP"}});
    CHECK(body.find("<u:DeletePortMapping xmlns:u=\"urn:schemas-upnp-org:service:WANIPConnection:1\">") != std::string::npos);
    CHECK(body.find("<NewRemoteHost></NewRemoteHost><NewExternalPort>7300</NewExternalPort><NewProtocol>UDP</NewProtocol>") != std::string::npos);
}

TEST_CASE("UPnP: private addresses")
{
    CHECK(isPrivateIPv4("192.168.1.10"));
    CHECK(isPrivateIPv4("10.0.0.1"));
    CHECK(isPrivateIPv4("172.20.1.1"));
    CHECK(isPrivateIPv4("100.72.1.2"));
    CHECK_FALSE(isPrivateIPv4("172.32.0.1"));
    CHECK_FALSE(isPrivateIPv4("203.0.113.5"));
    CHECK_FALSE(isPrivateIPv4("8.8.8.8"));
}
