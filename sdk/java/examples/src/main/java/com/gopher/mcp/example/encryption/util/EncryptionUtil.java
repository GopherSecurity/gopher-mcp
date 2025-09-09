package com.gopher.mcp.example.encryption.util;

import com.fasterxml.jackson.databind.ObjectMapper;
import io.modelcontextprotocol.spec.McpSchema;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.HashMap;
import java.util.Map;

public class EncryptionUtil {

    private static final Logger LOGGER = LoggerFactory.getLogger(EncryptionUtil.class);
    private static final String ENCRYPTION_PREFIX = "ENCRYPTED_START:";
    private static final String ENCRYPTION_SUFFIX = ":ENCRYPTED_END";
    private static final ObjectMapper objectMapper = new ObjectMapper();

    public static McpSchema.JSONRPCMessage encryptMessage(McpSchema.JSONRPCMessage message) {
        try {
            LOGGER.info("Starting encryption of message: {}", message.getClass().getSimpleName());

            // Convert message to JSON string
            String messageJson = objectMapper.writeValueAsString(message);
            LOGGER.debug("Message serialized to JSON, original value: [{}], length: {} chars",messageJson,  messageJson.length());

            // Add prefix and suffix (simple "encryption")
            String encryptedData = ENCRYPTION_PREFIX + messageJson + ENCRYPTION_SUFFIX;
            LOGGER.info("Message encrypted successfully, encrypted value: [{}], encrypted length: {} chars", encryptedData, encryptedData.length());

            // Create a new message with encrypted data
            Map<String, Object> params = new HashMap<>();
            params.put("data", encryptedData);
            params.put("original_type", message.getClass().getSimpleName());

            McpSchema.JSONRPCNotification encryptedMessage = new McpSchema.JSONRPCNotification(
                "2.0",
                "encrypted_message",
                params
            );

            return encryptedMessage;

        } catch (Exception e) {
            LOGGER.error("Failed to encrypt message: {}", e.getMessage(), e);
            // Return original message if encryption fails to maintain communication
            return message;
        }
    }

    public static McpSchema.JSONRPCMessage decryptMessage(McpSchema.JSONRPCMessage message) {
        try {
            LOGGER.info("Starting decryption of message: {}", message.getClass().getSimpleName());
            // Check if this is an encrypted message
            if (message instanceof McpSchema.JSONRPCNotification notification) {
                if ("encrypted_message".equals(notification.method()) && notification.params() instanceof Map) {
                    @SuppressWarnings("unchecked")
                    Map<String, Object> params = (Map<String, Object>) notification.params();
                    String encryptedData = (String) params.get("data");
                    String originalType = (String) params.get("original_type");

                    if (encryptedData != null) {
                        LOGGER.debug("Found encrypted data, encryptedData: [{}], length: {} chars, original type: {}",
                                encryptedData, encryptedData.length(), originalType);

                        // Remove prefix and suffix (simple "decryption")
                        if (encryptedData.startsWith(ENCRYPTION_PREFIX) && encryptedData.endsWith(ENCRYPTION_SUFFIX)) {
                            String decryptedJson = encryptedData.substring(
                                ENCRYPTION_PREFIX.length(),
                                encryptedData.length() - ENCRYPTION_SUFFIX.length()
                            );
                            LOGGER.info("Message decrypted successfully, decrypted value: [{}], decrypted length: {} chars",
                                    decryptedJson, decryptedJson.length());

                            // Convert back to appropriate message type based on original type
                            McpSchema.JSONRPCMessage decryptedMessage = deserializeToConcreteType(decryptedJson, originalType);
                            LOGGER.debug("Message deserialized to: {}", decryptedMessage.getClass().getSimpleName());

                            return decryptedMessage;
                        } else {
                            LOGGER.warn("Encrypted data does not have expected prefix/suffix format");
                        }
                    }
                }
            }

            // If not encrypted, return as is
            LOGGER.debug("Message not encrypted, returning as-is");
            return message;

        } catch (Exception e) {
            LOGGER.error("Failed to decrypt message: {}", e.getMessage(), e);
            // Return original message if decryption fails
            return message;
        }
    }

    private static McpSchema.JSONRPCMessage deserializeToConcreteType(String json, String originalType) throws Exception {
        Class<?> targetClass;

        // Map original type names to concrete classes
        switch (originalType) {
            case "JSONRPCRequest":
                targetClass = McpSchema.JSONRPCRequest.class;
                break;
            case "JSONRPCResponse":
                targetClass = McpSchema.JSONRPCResponse.class;
                break;
            case "JSONRPCNotification":
                targetClass = McpSchema.JSONRPCNotification.class;
                break;
            default:
                LOGGER.warn("Unknown message type: {}, attempting to deserialize as JSONRPCNotification", originalType);
                targetClass = McpSchema.JSONRPCNotification.class;
                break;
        }

        return (McpSchema.JSONRPCMessage) objectMapper.readValue(json, targetClass);
    }
}
