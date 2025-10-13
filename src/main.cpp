#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <sstream>
#include <zlib.h>
#include <vector>
#include <iomanip>
#include <openssl/sha.h>
#include <algorithm>
#include <ctime>
#include <curl/curl.h>
#include <regex>
#include "util.hpp"

int main(int argc, char *argv[]){
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cerr << "Logs from your program will appear here!\n";

    // Uncomment this block to pass the first stage
    //
    if (argc < 2) {
        std::cerr << "No command provided.\n";
        return EXIT_FAILURE;
    }
    
    std::string command = argv[1];
    
    if (command == "init") {
        try {
            std::filesystem::create_directory(".git");
            std::filesystem::create_directory(".git/objects");
            std::filesystem::create_directory(".git/refs");
    
            std::ofstream headFile(".git/HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            } else {
                std::cerr << "Failed to create .git/HEAD file.\n";
                return EXIT_FAILURE;
            }
    
            std::cout << "Initialized git directory\n";
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "cat-file") {
        if (argc < 4) {
            std::cerr << "Usage: cat-file -p <object>\n";
            return EXIT_FAILURE;
        }
        
        std::string flag = argv[2];
        std::string hash = argv[3];
        
        if (flag != "-p") {
            std::cerr << "Only -p flag is supported\n";
            return EXIT_FAILURE;
        }
        
        try {
            std::string objectData = readGitObject(hash);
            
            // Git object format: "type size\0content"
            size_t nullPos = objectData.find('\0');
            if (nullPos == std::string::npos) {
                std::cerr << "Invalid git object format\n";
                return EXIT_FAILURE;
            }
            
            std::string header = objectData.substr(0, nullPos);
            std::string content = objectData.substr(nullPos + 1);
            
            // For blob objects, just print the content
            std::cout << content;
            
        } catch (const std::exception& e) {
            std::cerr << "Error reading object: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "hash-object") {
        if (argc < 4) {
            std::cerr << "Usage: hash-object -w <file>\n";
            return EXIT_FAILURE;
        }
        
        std::string flag = argv[2];
        std::string filename = argv[3];
        
        if (flag != "-w") {
            std::cerr << "Only -w flag is supported\n";
            return EXIT_FAILURE;
        }
        
        try {
            // Read the file content
            std::ifstream file(filename);
            if (!file) {
                std::cerr << "Failed to open file: " << filename << '\n';
                return EXIT_FAILURE;
            }
            
            std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
            file.close();
            
            // Create the blob object and get the hash
            std::string hash = writeGitObject(content);
            
            // Print the hash
            std::cout << hash << '\n';
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating object: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "ls-tree") {
        if (argc < 4) {
            std::cerr << "Usage: ls-tree --name-only <tree>\n";
            return EXIT_FAILURE;
        }
        
        std::string flag = argv[2];
        std::string hash = argv[3];
        
        if (flag != "--name-only") {
            std::cerr << "Only --name-only flag is supported\n";
            return EXIT_FAILURE;
        }
        
        try {
            std::string objectData = readGitObject(hash);
            
            // Parse the tree object
            std::vector<TreeEntry> entries = parseTreeObject(objectData);
            
            // Sort entries by name (as Git does)
            std::sort(entries.begin(), entries.end(), 
                     [](const TreeEntry& a, const TreeEntry& b) {
                         return a.name < b.name;
                     });
            
            // Print just the names
            for (const auto& entry : entries) {
                std::cout << entry.name << '\n';
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error reading tree object: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "write-tree") {
        try {
            // Create tree object from current directory
            std::string hash = createTreeFromDirectory(".");
            
            // Print the hash
            std::cout << hash << '\n';
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating tree: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "commit-tree") {
        if (argc < 5) {
            std::cerr << "Usage: commit-tree <tree_sha> -m <message> or commit-tree <tree_sha> -p <commit_sha> -m <message>\n";
            return EXIT_FAILURE;
        }
        
        std::string treeHash = argv[2];
        std::string parentHash = "";
        std::string message = "";
        
        if (argc == 5) {
            // Format: commit-tree <tree_sha> -m <message>
            std::string messageFlag = argv[3];
            message = argv[4];
            
            if (messageFlag != "-m") {
                std::cerr << "Usage: commit-tree <tree_sha> -m <message>\n";
                return EXIT_FAILURE;
            }
        } else if (argc == 7) {
            // Format: commit-tree <tree_sha> -p <commit_sha> -m <message>
            std::string parentFlag = argv[3];
            parentHash = argv[4];
            std::string messageFlag = argv[5];
            message = argv[6];
            
            if (parentFlag != "-p" || messageFlag != "-m") {
                std::cerr << "Usage: commit-tree <tree_sha> -p <commit_sha> -m <message>\n";
                return EXIT_FAILURE;
            }
        } else {
            std::cerr << "Usage: commit-tree <tree_sha> -m <message> or commit-tree <tree_sha> -p <commit_sha> -m <message>\n";
            return EXIT_FAILURE;
        }
        
        try {
            // Create commit object
            std::string hash = writeCommitObject(treeHash, parentHash, message);
            
            // Print the hash
            std::cout << hash << '\n';
            
        } catch (const std::exception& e) {
            std::cerr << "Error creating commit: " << e.what() << '\n';
            return EXIT_FAILURE;
        }
    } else if (command == "clone") {
        if (argc < 4) {
            std::cerr << "Usage: clone <url> <directory>\n";
            return EXIT_FAILURE;
        }
        
        std::string url = argv[2];
        std::string targetDir = argv[3];
        
        try {
            // Initialize CURL
            curl_global_init(CURL_GLOBAL_ALL);
            
            // Clone the repository
            cloneRepository(url, targetDir);
            
            // Cleanup CURL
            curl_global_cleanup();
            
        } catch (const std::exception& e) {
            std::cerr << "Error cloning repository: " << e.what() << '\n';
            curl_global_cleanup();
            return EXIT_FAILURE;
        }
    } else {
        std::cerr << "Unknown command " << command << '\n';
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}