#ifndef RETE_SERIALIZER_HPP
#define RETE_SERIALIZER_HPP

#include "rete_node.hpp"

#include <nlohmann/json.hpp>
#include <string>

class StatefulSession;   // Forward declaration
class ReteNode;

/**
 * @class ReteSerializer
 * @brief Handles serialization and deserialization of Rete network sessions.
 *
 * The ReteSerializer class provides functionality to serialize a Rete network session
 * into a JSON string and to deserialize a session from a JSON string. It supports both
 * const and non-const StatefulSession objects, allowing for flexible usage in different
 * contexts. Internally, it manages pointers to the session to handle const-correctness.
 *
 * @note Uses nlohmann::json for JSON serialization.
 */
class ReteSerializer {
public:
    // Overloads for both const and non-const sessions
    explicit ReteSerializer(StatefulSession& session);
    explicit ReteSerializer(StatefulSession const& session);

    /**
     * @brief Serializes the current object to a string representation.
     *
     * @return A std::string containing the serialized form of the object.
     */
    std::string serialize() const;
    /**
     * @brief Deserializes the given JSON data and updates the object's state.
     *
     * This function parses the provided JSON string and populates the object's
     * members accordingly. It expects the input string to be a valid JSON
     * representation of the object's data.
     *
     * @param json_data A constant reference to a string containing JSON data.
     */
    void deserialize(std::string const& json_data);

private:
    /**
     * @brief Serializes a ReteNode into a JSON object.
     *
     * Converts the given shared pointer to a constant ReteNode into its
     * corresponding nlohmann::json representation. This function is useful
     * for persisting or transmitting the structure and data of a ReteNode.
     *
     * @param node A shared pointer to the constant ReteNode to serialize.
     * @return nlohmann::json The JSON representation of the provided node.
     */
    nlohmann::json node_to_json(std::shared_ptr<ReteNode const> node) const;

    // Use a pointer to avoid slicing and handle const-ness
    StatefulSession* session_;
    StatefulSession const* const_session_;
};

#endif   // RETE_SERIALIZER_HPP
