#include <gtest/gtest.h>

#include "utils/capiocl_adapter.hpp"

extern capiocl::engine::Engine *capio_cl_engine;

// A step listed as producer of "dir/*" creates "dir", and stats it first: the server must
// answer it, not make it wait for a directory only it will create.
TEST(DirectoryProducerTest, ProducerOfTheFilesInADirectoryProducesTheDirectory) {
    std::string files = "/workflow/out/*", writer = "writer";
    capio_cl_engine->newFile(files);
    capio_cl_engine->addProducer(files, writer);

    EXPECT_TRUE(produces_path_or_contents("/workflow/out", "writer"));
    EXPECT_TRUE(produces_path_or_contents("/workflow/out/part", "writer"));
    EXPECT_FALSE(produces_path_or_contents("/workflow/out", "reader"));
    EXPECT_FALSE(produces_path_or_contents("/workflow/other", "writer"));
}
