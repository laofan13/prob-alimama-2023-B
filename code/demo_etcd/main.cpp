#include <etcd/Client.hpp>
#include <etcd/Response.hpp>
#include <string>
#include <iostream>

std::string str_url = "http://etcd:2379";
std::string str_key = "a";

int main() {
    etcd::Client etcd(str_url);
    while (1) {
        etcd::Response response = etcd.get(str_key).get();
        std::cout << str_key << " :" << response.value().as_string()<<"\n";
        sleep(3);
    }
    return 0;
}
