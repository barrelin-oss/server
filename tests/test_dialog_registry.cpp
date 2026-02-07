// test_dialog_registry.cpp
// Unit tests for dialog registry YAML loading

#include "registry/dialog_registry.h"

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace hb {

class dialog_registry_test : public ::testing::Test {
protected:
    void SetUp() override {
        registry_.initialize();
    }

    void TearDown() override {
        registry_.shutdown();
        if (std::filesystem::exists(temp_path_)) {
            std::filesystem::remove(temp_path_);
        }
    }

    void write_yaml(const std::string& content) {
        std::ofstream f(temp_path_);
        f << content;
        f.close();
    }

    dialog_registry registry_;
    std::filesystem::path temp_path_{"test_dialogs_tmp.yaml"};
};

TEST_F(dialog_registry_test, load_valid_yaml) {
    write_yaml(R"(
dialogs:
  Howard:
    greeting: "Welcome, traveler."
    nodes:
      start:
        text: "What brings you here?"
        options:
          - { label: "Tell me about this town", next: about_town }
          - { label: "Goodbye", action: close }
      about_town:
        text: "This is a great town."
        options:
          - { label: "Back", next: start }
          - { label: "Goodbye", action: close }
)");

    auto result = registry_.load_from_file(temp_path_);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 1u);
    EXPECT_EQ(registry_.count(), 1u);
}

TEST_F(dialog_registry_test, get_dialog_by_name) {
    write_yaml(R"(
dialogs:
  TestNPC:
    greeting: "Hello!"
    nodes:
      start:
        text: "How can I help?"
        options:
          - { label: "Bye", action: close }
)");

    registry_.load_from_file(temp_path_);
    auto* dialog = registry_.get_dialog("TestNPC");
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->npc_name, "TestNPC");
    EXPECT_EQ(dialog->greeting, "Hello!");
    EXPECT_EQ(dialog->start_node, "start");
    EXPECT_EQ(dialog->nodes.size(), 1u);
}

TEST_F(dialog_registry_test, get_missing_dialog_returns_nullptr) {
    write_yaml(R"(
dialogs:
  ExistingNPC:
    greeting: "Hi"
    nodes:
      start:
        text: "Hello"
        options:
          - { label: "Bye", action: close }
)");

    registry_.load_from_file(temp_path_);
    EXPECT_EQ(registry_.get_dialog("NonExistent"), nullptr);
}

TEST_F(dialog_registry_test, get_node_by_id) {
    write_yaml(R"(
dialogs:
  TestNPC:
    greeting: "Welcome"
    nodes:
      start:
        text: "First node"
        options:
          - { label: "Next", next: second }
      second:
        text: "Second node"
        options:
          - { label: "Back", next: start }
)");

    registry_.load_from_file(temp_path_);

    auto* node = registry_.get_node("TestNPC", "start");
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->id, "start");
    EXPECT_EQ(node->text, "First node");
    EXPECT_EQ(node->options.size(), 1u);
    EXPECT_EQ(node->options[0].label, "Next");
    EXPECT_EQ(node->options[0].action, npc::dialog_action::goto_node);
    EXPECT_EQ(node->options[0].next_node, "second");

    auto* node2 = registry_.get_node("TestNPC", "second");
    ASSERT_NE(node2, nullptr);
    EXPECT_EQ(node2->text, "Second node");
}

TEST_F(dialog_registry_test, get_missing_node_returns_nullptr) {
    write_yaml(R"(
dialogs:
  TestNPC:
    greeting: "Hi"
    nodes:
      start:
        text: "Only node"
        options:
          - { label: "Bye", action: close }
)");

    registry_.load_from_file(temp_path_);
    EXPECT_EQ(registry_.get_node("TestNPC", "nonexistent"), nullptr);
    EXPECT_EQ(registry_.get_node("NonExistent", "start"), nullptr);
}

TEST_F(dialog_registry_test, dialog_action_types) {
    write_yaml(R"(
dialogs:
  MultiAction:
    greeting: "Welcome"
    nodes:
      start:
        text: "Choose"
        options:
          - { label: "Shop", action: open_shop }
          - { label: "Bank", action: open_bank }
          - { label: "Quests", action: open_quests }
          - { label: "Citizen", action: offer_citizenship }
)");

    registry_.load_from_file(temp_path_);
    auto* node = registry_.get_node("MultiAction", "start");
    ASSERT_NE(node, nullptr);
    ASSERT_EQ(node->options.size(), 4u);
    EXPECT_EQ(node->options[0].action, npc::dialog_action::open_shop);
    EXPECT_EQ(node->options[1].action, npc::dialog_action::open_bank);
    EXPECT_EQ(node->options[2].action, npc::dialog_action::open_quests);
    EXPECT_EQ(node->options[3].action, npc::dialog_action::offer_citizenship);
}

TEST_F(dialog_registry_test, custom_start_node) {
    write_yaml(R"(
dialogs:
  CustomStart:
    greeting: "Hello"
    start_node: intro
    nodes:
      intro:
        text: "Custom start"
        options:
          - { label: "Continue", next: main }
      main:
        text: "Main dialog"
        options:
          - { label: "Bye", action: close }
)");

    registry_.load_from_file(temp_path_);
    auto* dialog = registry_.get_dialog("CustomStart");
    ASSERT_NE(dialog, nullptr);
    EXPECT_EQ(dialog->start_node, "intro");
}

TEST_F(dialog_registry_test, invalid_yaml_returns_error) {
    write_yaml("not: valid: yaml: [[[");
    auto result = registry_.load_from_file(temp_path_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(dialog_registry_test, missing_dialogs_section_returns_error) {
    write_yaml("something_else: true");
    auto result = registry_.load_from_file(temp_path_);
    EXPECT_TRUE(result.is_err());
}

TEST_F(dialog_registry_test, multiple_npcs) {
    write_yaml(R"(
dialogs:
  NPC1:
    greeting: "Hello 1"
    nodes:
      start:
        text: "NPC 1"
        options:
          - { label: "Bye", action: close }
  NPC2:
    greeting: "Hello 2"
    nodes:
      start:
        text: "NPC 2"
        options:
          - { label: "Bye", action: close }
  NPC3:
    greeting: "Hello 3"
    nodes:
      start:
        text: "NPC 3"
        options:
          - { label: "Bye", action: close }
)");

    auto result = registry_.load_from_file(temp_path_);
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(result.value(), 3u);
    EXPECT_NE(registry_.get_dialog("NPC1"), nullptr);
    EXPECT_NE(registry_.get_dialog("NPC2"), nullptr);
    EXPECT_NE(registry_.get_dialog("NPC3"), nullptr);
}

}  // namespace hb
