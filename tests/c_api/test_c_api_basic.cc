/**
 * @file test_c_api_basic.cc
 * @brief Basic unit tests for MCP C API
 * 
 * Tests only the implemented functionality
 */

#include <gtest/gtest.h>
#include "mcp/c_api/mcp_c_types.h"
#include <cstring>

class MCPCApiBasicTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Any setup needed
    }
    
    void TearDown() override {
        // Cleanup
    }
};

// Test text content block creation
TEST_F(MCPCApiBasicTest, CreateTextContent) {
    // Create text content
    mcp_content_block_t* block = mcp_text_content_create("Hello, World!");
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->type, MCP_CONTENT_TEXT);
    ASSERT_NE(block->content.text, nullptr);
    EXPECT_STREQ(block->content.text->text.data, "Hello, World!");
    EXPECT_EQ(block->content.text->text.length, 13);
    
    // Validate type checking
    EXPECT_TRUE(mcp_content_block_is_text(block));
    EXPECT_FALSE(mcp_content_block_is_image(block));
    EXPECT_FALSE(mcp_content_block_is_audio(block));
    
    // Clean up
    mcp_content_block_free(block);
}

// Test image content block creation
TEST_F(MCPCApiBasicTest, CreateImageContent) {
    // Create image content
    mcp_content_block_t* block = mcp_image_content_create(
        "base64encodeddata", "image/png");
    
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->type, MCP_CONTENT_IMAGE);
    ASSERT_NE(block->content.image, nullptr);
    EXPECT_STREQ(block->content.image->data.data, "base64encodeddata");
    EXPECT_STREQ(block->content.image->mime_type.data, "image/png");
    
    // Validate type checking
    EXPECT_FALSE(mcp_content_block_is_text(block));
    EXPECT_TRUE(mcp_content_block_is_image(block));
    EXPECT_FALSE(mcp_content_block_is_audio(block));
    
    // Clean up
    mcp_content_block_free(block);
}

// Test audio content block creation
TEST_F(MCPCApiBasicTest, CreateAudioContent) {
    // Create audio content
    mcp_content_block_t* block = mcp_audio_content_create(
        "base64audiodata", "audio/mp3");
    
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(block->type, MCP_CONTENT_AUDIO);
    ASSERT_NE(block->content.audio, nullptr);
    EXPECT_STREQ(block->content.audio->data.data, "base64audiodata");
    EXPECT_STREQ(block->content.audio->mime_type.data, "audio/mp3");
    
    // Validate type checking
    EXPECT_FALSE(mcp_content_block_is_text(block));
    EXPECT_FALSE(mcp_content_block_is_image(block));
    EXPECT_TRUE(mcp_content_block_is_audio(block));
    
    // Clean up
    mcp_content_block_free(block);
}

// Test message creation
TEST_F(MCPCApiBasicTest, CreateMessage) {
    // Create a user message
    mcp_message_t* msg = mcp_user_message("Test message");
    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(msg->role, MCP_ROLE_USER);
    EXPECT_EQ(msg->content.type, MCP_CONTENT_TEXT);
    ASSERT_NE(msg->content.content.text, nullptr);
    EXPECT_STREQ(msg->content.content.text->text.data, "Test message");
    
    // Clean up
    mcp_message_free(msg);
    
    // Create an assistant message
    msg = mcp_assistant_message("Response");
    ASSERT_NE(msg, nullptr);
    EXPECT_EQ(msg->role, MCP_ROLE_ASSISTANT);
    EXPECT_EQ(msg->content.type, MCP_CONTENT_TEXT);
    ASSERT_NE(msg->content.content.text, nullptr);
    EXPECT_STREQ(msg->content.content.text->text.data, "Response");
    
    // Clean up
    mcp_message_free(msg);
}

// Test string utilities
TEST_F(MCPCApiBasicTest, StringUtilities) {
    mcp_string_t str;
    str.data = strdup("Test string");
    str.length = strlen(str.data);
    
    EXPECT_STREQ(str.data, "Test string");
    EXPECT_EQ(str.length, 11);
    
    // Free the string
    mcp_string_free(&str);
    EXPECT_EQ(str.data, nullptr);
    EXPECT_EQ(str.length, 0);
}