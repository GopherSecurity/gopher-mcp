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
};
