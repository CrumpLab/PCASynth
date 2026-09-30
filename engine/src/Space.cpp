#include "pcs/Space.h"

#include "pcs/Model.h"
#include "pcs/WaveModel.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace pcs {

int Space::soundIndex (const std::string& name) const
{
    const auto it = std::find (names.begin(), names.end(), name);
    return it == names.end() ? -1 : static_cast<int> (it - names.begin());
}

std::vector<float> Space::soundZ (int i) const
{
    std::vector<float> z (static_cast<size_t> (numComponents()));
    for (int j = 0; j < numComponents(); ++j)
        z[static_cast<size_t> (j)] = static_cast<float> (pca.score (i, j) / sd (j));
    return z;
}

std::shared_ptr<Space> deserializeSpace (const uint8_t* data, size_t size)
{
    if (size >= 4 && std::memcmp (data, "PCSW", 4) == 0)
        return std::make_shared<WaveModel> (deserializeWaveModel (data, size));
    return std::make_shared<Model> (deserializeModel (data, size));
}

std::vector<uint8_t> serializeSpace (const Space& space)
{
    if (const auto* w = dynamic_cast<const WaveModel*> (&space))
        return serializeWaveModel (*w);
    if (const auto* m = dynamic_cast<const Model*> (&space))
        return serializeModel (*m);
    throw std::invalid_argument ("unknown kind of space");
}

std::shared_ptr<Space> loadSpace (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        throw std::runtime_error ("cannot open " + path);
    std::vector<uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    return deserializeSpace (bytes.data(), bytes.size());
}

void saveSpace (const Space& space, const std::string& path)
{
    const auto bytes = serializeSpace (space);
    std::ofstream out (path, std::ios::binary);
    if (! out.write (reinterpret_cast<const char*> (bytes.data()), static_cast<std::streamsize> (bytes.size())))
        throw std::runtime_error ("cannot write " + path);
}

} // namespace pcs
