#pragma once

#include <cstdio>
#include <fstream>
#include <string>

namespace reader
{
    namespace detail
    {
        // Keep the temporary beside its destination so replacement stays atomic.
        template <class Write>
        bool atomic_write_stream(const std::string &path, Write write)
        {
            const auto temporary = path + ".tmp";
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            write(output);
            output.close();
            if (!output || std::rename(temporary.c_str(), path.c_str()) != 0)
            {
                std::remove(temporary.c_str());
                return false;
            }
            return true;
        }

        inline bool atomic_write(const std::string &path, const std::string &data)
        {
            return atomic_write_stream(path, [&](std::ostream &output)
                                       { output.write(data.data(), static_cast<std::streamsize>(data.size())); });
        }
    }
}