#pragma once

#include "reader.hpp"
#include <algorithm>
#include <atomic>
#include <exception>
#include <thread>

namespace reader
{
    struct SearchMatch
    {
        std::size_t chapter, offset;
        std::string preview;
    };

    class BookSearch
    {
    public:
        static constexpr std::size_t limit = 256;
        ~BookSearch() { cancel(); }

        void start(Publication publication, std::string query)
        {
            cancel();
            canceled_ = done_ = false;
            error.clear();
            matches.clear();
            unavailable = 0;
            fold(query);
            if (query.empty())
                return;
            worker_ = std::thread([this, publication = std::move(publication), query = std::move(query)]
                                  {
                try
                {
                    for (std::size_t chapter = 0; chapter < publication.chapters.size() && !canceled_ && matches.size() < limit; ++chapter)
                    {
                        Book book;
                        std::string message;
                        if (!load_chapter(publication, chapter, book, message))
                        {
                            ++unavailable;
                            continue;
                        }
                        auto folded = book.text;
                        fold(folded);
                        std::size_t offset = 0;
                        while (!canceled_ && matches.size() < limit && (offset = folded.find(query, offset)) != std::string::npos)
                        {
                            const auto end = offset + query.size();
                            const auto image = std::lower_bound(book.illustrations.begin(), book.illustrations.end(), offset,
                                                                [](const Illustration &item, std::size_t at) { return item.end <= at; });
                            if (image == book.illustrations.end() || image->offset >= end)
                                matches.push_back({chapter, offset, passage(book.text, offset, end)});
                            offset = end;
                        }
                    }
                }
                catch (const std::exception &)
                {
                    error = "Book search could not finish.";
                }
                done_ = true; });
        }

        bool pending() const { return worker_.joinable(); }
        bool poll()
        {
            if (!pending() || !done_)
                return false;
            worker_.join();
            return true;
        }

        void cancel()
        {
            canceled_ = true;
            if (worker_.joinable())
                worker_.join();
        }

        std::vector<SearchMatch> matches;
        std::size_t unavailable = 0;
        std::string error;

    private:
        static void fold(std::string &value)
        {
            for (auto &c : value)
                if (c >= 'A' && c <= 'Z')
                    c += 'a' - 'A';
        }

        static std::string passage(const std::string &text, std::size_t begin, std::size_t end)
        {
            // Keep the match near the start so it is visible even in a short result row.
            auto first = begin - std::min<std::size_t>(begin, 12);
            auto last = std::min(text.size(), std::min(end, begin + 96) + 60);
            while ((static_cast<unsigned char>(text[first]) & 0xc0) == 0x80)
                ++first;
            while (last < text.size() && (static_cast<unsigned char>(text[last]) & 0xc0) == 0x80)
                --last;
            auto result = text.substr(first, last - first);
            for (auto &c : result)
                if (c == '\n' || c == '\r' || c == '\t')
                    c = ' ';
            return result;
        }

        std::thread worker_;
        std::atomic<bool> canceled_{false}, done_{false};
    };
}
