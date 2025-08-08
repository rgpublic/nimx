#include <string>
#include <iostream>
#include <filesystem>
#include <sys/wait.h>

using namespace std;

int main(int argc, char **argv) {

    std::string home_dir = getenv("HOME");

    if (!std::filesystem::exists(home_dir+"/.nimx")) {
        std::filesystem::create_directory(home_dir+"/.nimx");
    }

    string app = argv[1];
    string path = std::filesystem::path(app).parent_path();
    string filename = std::filesystem::path(app).filename();
        const hash<string> hasher;
    std::stringstream ss;
    ss << std::hex << hasher(app);
    string output = home_dir+"/.nimx/hash_"+ss.str();

    std::string args;
    for (int i = 2; i < argc; ++i) {
        if (i > 2) args += ' ';
        std::string arg = argv[i];
        // Re-add quotes if the argument contains spaces
        if (arg.find(' ') != std::string::npos) {
            args += '"' + arg + '"';
        } else {
            args += arg;
        }
    }

    if (!std::filesystem::exists(output) || std::filesystem::last_write_time(app)>std::filesystem::last_write_time(output)) {
        string cmd = "cat "+app+" | nim -d:self:"+app+" -p="+path+" --warning:OctalEscape=off --warning:UnreachableCode=off --hints=off -d:ssl -o="+output+" c -";
        int ret=system(cmd.c_str());
        if (ret!=0) return 1;
    }
    string cmd2 = output+" "+args;
    int ret2=WEXITSTATUS(system(cmd2.c_str()));
    return ret2;
}
