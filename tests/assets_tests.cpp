#include "nint2/assets.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define CHECK(test) do { if (!(test)) { \
    std::cerr << "FAILED " << #test << " line " << __LINE__ << '\n'; \
    return EXIT_FAILURE; } } while (false)

int main() {
    using namespace nint2::assets;
    AssetStore store;
    for (const auto path : {"", "/", "relative", "/a/", "/a//b", "/../a",
                            "/a/../b", "/a/./b", "/back\\slash", "/space here",
                            "/name:with:colon", "/a?b"}) {
        CHECK(!AssetStore::ValidPath(path));
        CHECK(store.Mount(path, {}).error == Error::InvalidPath);
    }
    CHECK(!AssetStore::ValidPath(std::string("/a\0b", 4)));
    CHECK(!AssetStore::ValidPath(std::string(129, 'a')));
    CHECK(AssetStore::ValidPath("/a"));
    CHECK(AssetStore::ValidPath("/a/b.c-0_FOO"));

    constexpr std::array<std::uint8_t, 5> hello{'h','e','l','l','o'};
    CHECK(store.Mount("/z/data.txt", hello).Ok());
    CHECK(store.Mount("/z/data.txt", hello).error == Error::Duplicate);
    CHECK(store.Mount("/a/empty", {}).Ok());
    CHECK(store.AssetCount() == 2 && store.StoredBytes() == 5);
    CHECK(AssetStore::Hash(hello) != AssetStore::Hash({}));
    CHECK(store.Open("/missing").error == Error::NotFound);
    CHECK(store.Open("/a/./b").error == Error::InvalidPath);
    const auto d1 = store.Open("/z/data.txt");
    const auto d2 = store.Open("/z/data.txt");
    CHECK(d1.Ok() && d2.Ok() && d1.value >= AssetStore::kFirstHandle &&
          d2.value == d1.value + 1);
    CHECK(store.OpenCount() == 2);
    std::array<std::uint8_t, 10> buffer{};
    CHECK(store.Peek(static_cast<std::uint32_t>(d1.value), buffer).value == 5);
    CHECK(buffer[0] == 'h' && buffer[4] == 'o');
    CHECK(store.Consume(static_cast<std::uint32_t>(d1.value), 2).value == 2);
    CHECK(store.Peek(static_cast<std::uint32_t>(d1.value), buffer).value == 3);
    CHECK(buffer[0] == 'l');
    CHECK(store.Peek(static_cast<std::uint32_t>(d2.value), buffer).value == 5);
    CHECK(buffer[0] == 'h');
    CHECK(store.Consume(static_cast<std::uint32_t>(d1.value), 4).error ==
          Error::InvalidArgument);
    CHECK(store.Seek(static_cast<std::uint32_t>(d1.value), -1, 1).value == 1);
    CHECK(store.Seek(static_cast<std::uint32_t>(d1.value), -1, 0).error ==
          Error::InvalidArgument);
    CHECK(store.Seek(static_cast<std::uint32_t>(d1.value), -1, 2).value == 4);
    CHECK(store.Peek(static_cast<std::uint32_t>(d1.value), buffer).value == 1);
    CHECK(buffer[0] == 'o');
    CHECK(store.Seek(static_cast<std::uint32_t>(d1.value), 10, 0).value == 10);
    CHECK(store.Peek(static_cast<std::uint32_t>(d1.value), buffer).value == 0);
    CHECK(store.Seek(static_cast<std::uint32_t>(d1.value), INT64_MIN, 1).error ==
          Error::InvalidArgument);
    CHECK(store.Seek(static_cast<std::uint32_t>(d1.value), INT64_MAX, 0).error ==
          Error::InvalidArgument);
    CHECK(store.Seek(static_cast<std::uint32_t>(d1.value), 0, 3).error ==
          Error::InvalidArgument);
    CHECK(store.PeekAt(static_cast<std::uint32_t>(d1.value), 1, buffer).value == 4);
    CHECK(buffer[0] == 'e');
    CHECK(store.PeekAt(static_cast<std::uint32_t>(d1.value),
                       AssetStore::kMaxSeek + 1, buffer).error == Error::InvalidArgument);
    CHECK(store.PeekAt(static_cast<std::uint32_t>(d1.value), 0,
                       std::vector<std::uint8_t>(AssetStore::kMaxRead + 1)).error ==
          Error::InvalidArgument);
    Info info{};
    CHECK(store.Stat("/z/data.txt", info) == Error::None);
    CHECK(info.size == 5 && info.offset == 0 && info.digest == AssetStore::Hash(hello));
    CHECK(store.Fstat(static_cast<std::uint32_t>(d1.value), info) == Error::None);
    CHECK(info.offset == 10);
    std::string_view name;
    CHECK(store.Enumerate(0, name, info) && name == "/a/empty" && info.size == 0);
    CHECK(store.Enumerate(1, name, info) && name == "/z/data.txt" && info.size == 5);
    CHECK(!store.Enumerate(2, name, info));
    CHECK(store.Close(static_cast<std::uint32_t>(d1.value)).Ok());
    CHECK(store.Close(static_cast<std::uint32_t>(d1.value)).error == Error::InvalidHandle);
    CHECK(store.Fstat(static_cast<std::uint32_t>(d1.value), info) == Error::InvalidHandle);
    const auto d3 = store.Open("/z/data.txt");
    CHECK(d3.Ok() && d3.value > d2.value);
    store.CloseAll();
    CHECK(store.OpenCount() == 0 && store.Peek(static_cast<std::uint32_t>(d2.value),
          buffer).error == Error::InvalidHandle);

    AssetStore large;
    std::vector<std::uint8_t> too_large(AssetStore::kMaxFileBytes + 1);
    CHECK(large.Mount("/oversize", too_large).error == Error::TooLarge);
    CHECK(large.StoredBytes() == 0);
    std::vector<std::uint8_t> one_meg(AssetStore::kMaxFileBytes, 42);
    for (std::size_t i = 0; i < 16; ++i)
        CHECK(large.Mount("/asset" + std::to_string(i), one_meg).Ok());
    CHECK(large.StoredBytes() == AssetStore::kMaxTotalBytes);
    CHECK(large.Mount("/extra", hello).error == Error::NoSpace);
    CHECK(large.Mount("/zero", {}).Ok());
    CHECK(large.AssetCount() == 17);
    large.Seal();
    CHECK(large.Mount("/late", hello).error == Error::Sealed);
    CHECK(large.Open("/asset0").Ok());
    CHECK(large.Open("/late").error == Error::NotFound);

    AssetStore count_limit;
    for (std::size_t i = 0; i < AssetStore::kMaxAssets; ++i)
        CHECK(count_limit.Mount("/file" + std::to_string(i), {}).Ok());
    CHECK(count_limit.Mount("/exhausted", {}).error == Error::TooManyAssets);
    for (std::size_t i = 0; i < AssetStore::kMaxHandles; ++i)
        CHECK(count_limit.Open("/file0").Ok());
    CHECK(count_limit.Open("/file0").error == Error::TooManyHandles);

    std::cout << "Read-only asset manifest, integrity, capacities and cursors passed\n";
    return EXIT_SUCCESS;
}
