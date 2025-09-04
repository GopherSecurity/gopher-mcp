module.exports = {
  preset: "ts-jest",
  testEnvironment: "node",
  roots: ["<rootDir>/src"],
  testMatch: ["**/__tests__/**/*.ts", "**/?(*.)+(spec|test).ts"],
  transform: {
    "^.+\\.ts$": "ts-jest",
  },
  collectCoverageFrom: ["src/**/*.ts", "!src/**/*.d.ts", "!src/types/**/*.ts"],
  coverageDirectory: "coverage",
  coverageReporters: ["text", "lcov", "html"],

  testTimeout: 30000,
  moduleNameMapper: {
    "^@/(.*)$": "<rootDir>/src/$1",
  },

  // Fix koffi compatibility issues
  globals: {
    "ts-jest": {
      // Disable type checking in tests for better performance
      isolatedModules: true,
    },
  },

  // Preserve object extensibility for koffi
  setupFilesAfterEnv: ["<rootDir>/jest.setup.js"],

  // Disable sandboxing that interferes with koffi
  testEnvironmentOptions: {
    // Allow native modules to work properly
    url: "http://localhost",
  },

  // Ensure koffi can access native modules
  transformIgnorePatterns: ["node_modules/(?!(koffi)/)"],

  // Disable automatic mocking that might interfere with koffi
  clearMocks: true,
  restoreMocks: true,
};
