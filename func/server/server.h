#pragma once
#include <string>

/**
 * @brief Starts a static file server.
 * 
 * @param root_path The directory to serve files from.
 * @param port The port to listen on.
 * @return int 0 on success, non-zero on failure.
 */
int start_server(const std::string& root_path, int port);
