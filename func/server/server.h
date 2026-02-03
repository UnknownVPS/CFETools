#pragma once
#include <string>

/**
 * @brief Starts a static file server.
 * 
 * @param root_path The directory to serve files from.
 * @param port The port to listen on.
 * @param page_size Number of items per page for directory listings.
 * @param thread_pool_size Number of threads to use for handling requests.
 * @param symlinks_enabled Whether to follow symbolic links.
 * @return int 0 on success, non-zero on failure.
 */
int start_server(const std::string& root_path, int port, size_t page_size, int thread_pool_size, bool symlinks_enabled = false);
