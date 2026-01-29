#pragma once

// repository.h
// Generic repository interface for data persistence

#include "core/types.h"
#include "core/result.h"

#include <optional>
#include <vector>
#include <string>
#include <functional>

namespace hb::persistence {

// Repository error types
enum class repository_error : uint8_t {
    success = 0,
    not_found = 1,
    already_exists = 2,
    save_failed = 3,
    load_failed = 4,
    delete_failed = 5,
    connection_error = 6,
    validation_error = 7,
    unknown_error = 8,
};

// Generic repository interface
template<typename T, typename Id>
class repository {
public:
    virtual ~repository() = default;

    // CRUD operations
    virtual auto save(const T& entity) -> result<void, repository_error> = 0;
    virtual auto load(Id id) -> result<T, repository_error> = 0;
    virtual auto remove(Id id) -> result<void, repository_error> = 0;
    virtual auto exists(Id id) -> bool = 0;

    // Bulk operations
    virtual auto load_all() -> result<std::vector<T>, repository_error> = 0;
    virtual auto save_all(const std::vector<T>& entities) -> result<void, repository_error> = 0;

    // Query support (optional)
    virtual auto count() -> size_t = 0;
};

// Base in-memory repository implementation
template<typename T, typename Id, typename Hash = std::hash<Id>>
class memory_repository : public repository<T, Id> {
public:
    using entity_type = T;
    using id_type = Id;
    using id_extractor = std::function<Id(const T&)>;

    explicit memory_repository(id_extractor get_id)
        : get_id_(std::move(get_id)) {}

    auto save(const T& entity) -> result<void, repository_error> override {
        Id id = get_id_(entity);
        data_[id] = entity;
        return result<void, repository_error>::ok();
    }

    auto load(Id id) -> result<T, repository_error> override {
        auto it = data_.find(id);
        if (it == data_.end()) {
            return result<T, repository_error>::err(repository_error::not_found);
        }
        return result<T, repository_error>::ok(it->second);
    }

    auto remove(Id id) -> result<void, repository_error> override {
        auto erased = data_.erase(id);
        if (erased == 0) {
            return result<void, repository_error>::err(repository_error::not_found);
        }
        return result<void, repository_error>::ok();
    }

    auto exists(Id id) -> bool override {
        return data_.contains(id);
    }

    auto load_all() -> result<std::vector<T>, repository_error> override {
        std::vector<T> entities;
        entities.reserve(data_.size());
        for (const auto& [id, entity] : data_) {
            entities.push_back(entity);
        }
        return result<std::vector<T>, repository_error>::ok(std::move(entities));
    }

    auto save_all(const std::vector<T>& entities) -> result<void, repository_error> override {
        for (const auto& entity : entities) {
            Id id = get_id_(entity);
            data_[id] = entity;
        }
        return result<void, repository_error>::ok();
    }

    auto count() -> size_t override {
        return data_.size();
    }

    void clear() {
        data_.clear();
    }

protected:
    std::unordered_map<Id, T, Hash> data_;
    id_extractor get_id_;
};

}  // namespace hb::persistence
