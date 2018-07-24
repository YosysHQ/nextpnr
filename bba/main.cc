#include <map>
#include <vector>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

enum TokenType : int8_t
{
    TOK_LABEL,
    TOK_REF,
    TOK_U8,
    TOK_U16,
    TOK_U32
};

struct Stream
{
    std::string name;
    std::vector<TokenType> tokenTypes;
    std::vector<uint32_t> tokenValues;
    std::vector<std::string> tokenComments;
};

Stream stringStream;
std::vector<Stream> streams;
std::map<std::string, int> streamIndex;
std::vector<int> streamStack;

std::vector<int> labels;
std::map<std::string, int> labelIndex;

std::vector<std::string> preText, postText;

const char *skipWhitespace(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

int main()
{
    bool verbose = true;
    // bool bigEndian = false;
    char buffer[512];

    while (fgets(buffer, 512, stdin) != nullptr)
    {
        std::string cmd = strtok(buffer, " \t\r\n");

        if (cmd == "pre") {
            const char *p = skipWhitespace(strtok(nullptr, "\r\n"));
            preText.push_back(p);
            continue;
        }

        if (cmd == "post") {
            const char *p = skipWhitespace(strtok(nullptr, "\r\n"));
            postText.push_back(p);
            continue;
        }

        if (cmd == "push") {
            const char *p = strtok(buffer, " \t\r\n");
            if (streamIndex.count(p) == 0) {
                streamIndex[p] = streams.size();
                streams.resize(streams.size() + 1);
                streams.back().name = p;
            }
            streamStack.push_back(streamIndex.at(p));
            continue;
        }

        if (cmd == "pop") {
            streamStack.pop_back();
            continue;
        }

        if (cmd == "label" || cmd == "ref") {
            const char *label = strtok(buffer, " \t\r\n");
            const char *comment = skipWhitespace(strtok(buffer, "\r\n"));
            Stream &s = streams.at(streamStack.back());
            if (labelIndex.count(label) == 0) {
                labelIndex[label] = labels.size();
                labels.push_back(-1);
            }
            s.tokenTypes.push_back(cmd == "label" ? TOK_LABEL : TOK_REF);
            s.tokenValues.push_back(labelIndex.at(label));
            if (verbose)
                s.tokenComments.push_back(comment);
            continue;
        }

        if (cmd == "u8" || cmd == "u16" || cmd == "u32") {
            const char *value = strtok(buffer, " \t\r\n");
            const char *comment = skipWhitespace(strtok(buffer, "\r\n"));
            Stream &s = streams.at(streamStack.back());
            s.tokenTypes.push_back(cmd == "u8" ? TOK_U8 : cmd == "u16" ? TOK_U16 : TOK_U32);
            s.tokenValues.push_back(atoll(value));
            if (verbose)
                s.tokenComments.push_back(comment);
            continue;
        }

        if (cmd == "s") {
            const char *value = skipWhitespace(strtok(buffer, "\r\n"));
            std::string label = std::string("str:") + value;
            Stream &s = streams.at(streamStack.back());
            if (labelIndex.count(label) == 0) {
                labelIndex[label] = labels.size();
                labels.push_back(-1);
            }
            s.tokenTypes.push_back(TOK_REF);
            s.tokenValues.push_back(labelIndex.at(label));
            if (verbose)
                s.tokenComments.push_back(value);
            stringStream.tokenTypes.push_back(TOK_LABEL);
            stringStream.tokenValues.push_back(labelIndex.at(label));
            while (1) {
                stringStream.tokenTypes.push_back(TOK_U8);
                stringStream.tokenValues.push_back(*value);
                if (*value == 0)
                    break;
                value++;
            }
            continue;
        }

        abort();
    }

    assert(!streams.empty());
    assert(streamStack.empty());
    streams.push_back(Stream());
    streams.back().tokenTypes.swap(stringStream.tokenTypes);
    streams.back().tokenValues.swap(stringStream.tokenValues);

    return 0;
}
