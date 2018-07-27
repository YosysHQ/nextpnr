/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2018  Clifford Wolf <clifford@symbioticeda.com>
 *  Copyright (C) 2018  Miodrag Milanovic <miodrag@symbioticeda.com>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <assert.h>
#include <boost/program_options.hpp>
#include <iostream>
#include <map>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

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
std::vector<std::string> labelNames;
std::map<std::string, int> labelIndex;

std::vector<std::string> preText, postText;

const char *skipWhitespace(const char *p)
{
    if (p == nullptr)
        return "";
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

int main(int argc, char **argv)
{
    bool debug = false;
    bool verbose = false;
    bool bigEndian = false;
    bool writeC = false;
    char buffer[512];

    namespace po = boost::program_options;
    po::positional_options_description pos;
    po::options_description options("Allowed options");
    options.add_options()("v", "verbose output");
    options.add_options()("d", "debug output");
    options.add_options()("b", "big endian");
    options.add_options()("c", "write c strings");
    options.add_options()("files", po::value<std::vector<std::string>>(), "file parameters");
    pos.add("files", -1);

    po::variables_map vm;
    try {
        po::parsed_options parsed = po::command_line_parser(argc, argv).options(options).positional(pos).run();

        po::store(parsed, vm);

        po::notify(vm);
    } catch (std::exception &e) {
        std::cout << e.what() << "\n";
        return 1;
    }
    if (vm.count("v"))
        verbose = true;
    if (vm.count("d"))
        debug = true;
    if (vm.count("b"))
        bigEndian = true;
    if (vm.count("c"))
        writeC = true;

    if (vm.count("files") == 0) {
        printf("File parameters are mandatory\n");
        exit(-1);
    }
    std::vector<std::string> files = vm["files"].as<std::vector<std::string>>();
    if (files.size() != 2) {
        printf("Input and output parameters must be set\n");
        exit(-1);
    }

    FILE *fileIn = fopen(files.at(0).c_str(), "rt");
    assert(fileIn != nullptr);

    FILE *fileOut = fopen(files.at(1).c_str(), writeC ? "wt" : "wb");
    assert(fileOut != nullptr);

    while (fgets(buffer, 512, fileIn) != nullptr) {
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
            const char *p = strtok(nullptr, " \t\r\n");
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
            const char *label = strtok(nullptr, " \t\r\n");
            const char *comment = skipWhitespace(strtok(nullptr, "\r\n"));
            Stream &s = streams.at(streamStack.back());
            if (labelIndex.count(label) == 0) {
                labelIndex[label] = labels.size();
                if (debug)
                    labelNames.push_back(label);
                labels.push_back(-1);
            }
            s.tokenTypes.push_back(cmd == "label" ? TOK_LABEL : TOK_REF);
            s.tokenValues.push_back(labelIndex.at(label));
            if (debug)
                s.tokenComments.push_back(comment);
            continue;
        }

        if (cmd == "u8" || cmd == "u16" || cmd == "u32") {
            const char *value = strtok(nullptr, " \t\r\n");
            const char *comment = skipWhitespace(strtok(nullptr, "\r\n"));
            Stream &s = streams.at(streamStack.back());
            s.tokenTypes.push_back(cmd == "u8" ? TOK_U8 : cmd == "u16" ? TOK_U16 : TOK_U32);
            s.tokenValues.push_back(atoll(value));
            if (debug)
                s.tokenComments.push_back(comment);
            continue;
        }

        if (cmd == "str") {
            const char *value = skipWhitespace(strtok(nullptr, "\r\n"));
            char terminator[2] = {*value, 0};
            assert(terminator[0] != 0);
            value = strtok((char *)value + 1, terminator);
            const char *comment = skipWhitespace(strtok(nullptr, "\r\n"));
            std::string label = std::string("str:") + value;
            Stream &s = streams.at(streamStack.back());
            if (labelIndex.count(label) == 0) {
                labelIndex[label] = labels.size();
                if (debug)
                    labelNames.push_back(label);
                labels.push_back(-1);
            }
            s.tokenTypes.push_back(TOK_REF);
            s.tokenValues.push_back(labelIndex.at(label));
            if (debug)
                s.tokenComments.push_back(comment);
            stringStream.tokenTypes.push_back(TOK_LABEL);
            stringStream.tokenValues.push_back(labelIndex.at(label));
            stringStream.tokenComments.push_back("");
            while (1) {
                stringStream.tokenTypes.push_back(TOK_U8);
                stringStream.tokenValues.push_back(*value);
                if (debug) {
                    char char_comment[4] = {'\'', *value, '\'', 0};
                    if (*value < 32 || *value >= 127)
                        char_comment[0] = 0;
                    stringStream.tokenComments.push_back(char_comment);
                }
                if (*value == 0)
                    break;
                value++;
            }
            continue;
        }

        assert(0);
    }

    if (verbose) {
        printf("Constructed %d streams:\n", int(streams.size()));
        for (auto &s : streams)
            printf("    stream '%s' with %d tokens\n", s.name.c_str(), int(s.tokenTypes.size()));
    }

    assert(!streams.empty());
    assert(streamStack.empty());
    streams.push_back(Stream());
    streams.back().name = "strings";
    streams.back().tokenTypes.swap(stringStream.tokenTypes);
    streams.back().tokenValues.swap(stringStream.tokenValues);
    streams.back().tokenComments.swap(stringStream.tokenComments);

    int cursor = 0;
    for (auto &s : streams) {
        for (int i = 0; i < int(s.tokenTypes.size()); i++) {
            switch (s.tokenTypes[i]) {
            case TOK_LABEL:
                labels[s.tokenValues[i]] = cursor;
                break;
            case TOK_REF:
                cursor += 4;
                break;
            case TOK_U8:
                cursor += 1;
                break;
            case TOK_U16:
                assert(cursor % 2 == 0);
                cursor += 2;
                break;
            case TOK_U32:
                assert(cursor % 4 == 0);
                cursor += 4;
                break;
            default:
                assert(0);
            }
        }
    }

    if (verbose) {
        printf("resolved positions for %d labels.\n", int(labels.size()));
        printf("total data (including strings): %.2f MB\n", double(cursor) / (1024 * 1024));
    }

    std::vector<uint8_t> data(cursor);

    cursor = 0;
    for (auto &s : streams) {
        if (debug)
            printf("-- %s --\n", s.name.c_str());

        for (int i = 0; i < int(s.tokenTypes.size()); i++) {
            uint32_t value = s.tokenValues[i];
            int numBytes = 0;

            switch (s.tokenTypes[i]) {
            case TOK_LABEL:
                break;
            case TOK_REF:
                value = labels[value] - cursor;
                numBytes = 4;
                break;
            case TOK_U8:
                numBytes = 1;
                break;
            case TOK_U16:
                numBytes = 2;
                break;
            case TOK_U32:
                numBytes = 4;
                break;
            default:
                assert(0);
            }

            if (bigEndian) {
                switch (numBytes) {
                case 4:
                    data[cursor++] = value >> 24;
                    data[cursor++] = value >> 16;
                /* fall-through */
                case 2:
                    data[cursor++] = value >> 8;
                /* fall-through */
                case 1:
                    data[cursor++] = value;
                /* fall-through */
                case 0:
                    break;
                default:
                    assert(0);
                }
            } else {
                switch (numBytes) {
                case 4:
                    data[cursor + 3] = value >> 24;
                    data[cursor + 2] = value >> 16;
                /* fall-through */
                case 2:
                    data[cursor + 1] = value >> 8;
                /* fall-through */
                case 1:
                    data[cursor] = value;
                /* fall-through */
                case 0:
                    break;
                default:
                    assert(0);
                }
                cursor += numBytes;
            }

            if (debug) {
                printf("%08x ", cursor - numBytes);
                for (int k = cursor - numBytes; k < cursor; k++)
                    printf("%02x ", data[k]);
                for (int k = numBytes; k < 4; k++)
                    printf("   ");

                unsigned long long v = s.tokenValues[i];

                switch (s.tokenTypes[i]) {
                case TOK_LABEL:
                    if (s.tokenComments[i].empty())
                        printf("label %s\n", labelNames[v].c_str());
                    else
                        printf("label %-24s %s\n", labelNames[v].c_str(), s.tokenComments[i].c_str());
                    break;
                case TOK_REF:
                    if (s.tokenComments[i].empty())
                        printf("ref %s\n", labelNames[v].c_str());
                    else
                        printf("ref %-26s %s\n", labelNames[v].c_str(), s.tokenComments[i].c_str());
                    break;
                case TOK_U8:
                    if (s.tokenComments[i].empty())
                        printf("u8 %llu\n", v);
                    else
                        printf("u8 %-27llu %s\n", v, s.tokenComments[i].c_str());
                    break;
                case TOK_U16:
                    if (s.tokenComments[i].empty())
                        printf("u16 %-26llu\n", v);
                    else
                        printf("u16 %-26llu %s\n", v, s.tokenComments[i].c_str());
                    break;
                case TOK_U32:
                    if (s.tokenComments[i].empty())
                        printf("u32 %-26llu\n", v);
                    else
                        printf("u32 %-26llu %s\n", v, s.tokenComments[i].c_str());
                    break;
                default:
                    assert(0);
                }
            }
        }
    }

    assert(cursor == int(data.size()));

    if (writeC) {
        for (auto &s : preText)
            fprintf(fileOut, "%s\n", s.c_str());

        fprintf(fileOut, "const char %s[%d] =\n\"", streams[0].name.c_str(), int(data.size()) + 1);

        cursor = 1;
        for (int i = 0; i < int(data.size()); i++) {
            auto d = data[i];
            if (cursor > 70) {
                fputc('\"', fileOut);
                fputc('\n', fileOut);
                cursor = 0;
            }
            if (cursor == 0) {
                fputc('\"', fileOut);
                cursor = 1;
            }
            if (d < 32 || d >= 127) {
                if (i + 1 < int(data.size()) && (data[i + 1] < '0' || '9' < data[i + 1]))
                    cursor += fprintf(fileOut, "\\%o", int(d));
                else
                    cursor += fprintf(fileOut, "\\%03o", int(d));
            } else if (d == '\"' || d == '\'' || d == '\\') {
                fputc('\\', fileOut);
                fputc(d, fileOut);
                cursor += 2;
            } else {
                fputc(d, fileOut);
                cursor++;
            }
        }

        fprintf(fileOut, "\";\n");

        for (auto &s : postText)
            fprintf(fileOut, "%s\n", s.c_str());
    } else {
        fwrite(data.data(), int(data.size()), 1, fileOut);
    }

    return 0;
}
