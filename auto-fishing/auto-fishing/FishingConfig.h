#pragma once

// Fishing Configuration Constants
class FishingConfig {
public:
    // Time related constants (seconds)
    static constexpr double DEFAULT_CAST_TIME = 0.5;
    static constexpr double MIN_CAST_TIME = 0.2;
    static constexpr double MAX_CAST_TIME = 2.0;
    static constexpr double DEFAULT_REST_TIME = 0.5;
    static constexpr double MIN_REST_TIME = 0.1;
    static constexpr double MAX_REST_TIME = 10.0;
    static constexpr double DEFAULT_TIMEOUT_MINUTES = 1.0;
    static constexpr double MIN_TIMEOUT_MINUTES = 0.5;
    static constexpr double MAX_TIMEOUT_MINUTES = 15.0;

    // Detection related constants
    static constexpr double FISH_PICKUP_WAIT_TIME = 2.0;
    static constexpr double FISH_PICKUP_TIMEOUT = 30.0;
    static constexpr double BUCKET_WAIT_TIMEOUT = 10.0;
    static constexpr double TIMEOUT_REEL_WAIT = 10.0;
    static constexpr double RESTART_WAIT_TIME = 2.5;
    static constexpr double CAST_WAIT_TIME = 3.0;
    static constexpr double FORCE_REEL_REST = 1.0;
    static constexpr double CYCLE_COOLDOWN = 2.0;

    // Log check intervals
    static constexpr double LOG_CHECK_INTERVAL = 0.25;
    static constexpr double BUCKET_CHECK_INTERVAL = 0.5;
    static constexpr double PICKUP_CHECK_INTERVAL = 0.5;

    // Reel timeout (seconds)
    static constexpr double MAX_REEL_TIME = 30.0;

    // Debounce time after cast (seconds)
    static constexpr double DEBOUNCE_AFTER_CAST = 3.0;

    // Version info
    static constexpr const char* VERSION = "2.3.0";
};