#include <iostream>
#include <App.h>
#include <uv.h>
#include "LegalChess.h"


#include <string>
#include <sstream>
#include <fstream>

#include "Controller.h"

void initRoutes(uWS::SSLApp& app);

std::shared_ptr<uv_loop_t> uv_loop = nullptr;

int main() {
    LC::compute();

    uv_loop.reset(uv_default_loop());

    if(!uv_loop) std::cout << "loop null\n";

    uWS::Loop::get(uv_loop.get());
    uWS::SSLApp app;
    
    initRoutes(app);

    app.listen("0.0.0.0", 8080, [](auto * socket){
        if(!socket) std::cout << "Couldn't start the sever\n";
        else std::cout << "Server running at port 8080\n";
    });

    app.run();

    return 0;
}