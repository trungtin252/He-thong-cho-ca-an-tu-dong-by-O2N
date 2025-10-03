CREATE DATABASE feeding_system CHARACTER SET utf8mb4;

USE feeding_system;

CREATE TABLE logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    log_date VARCHAR(20),
    data TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE schedules (
    id INT AUTO_INCREMENT PRIMARY KEY,
    schedule_data TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE status (
    id INT AUTO_INCREMENT PRIMARY KEY,
    wifi_rssi INT,
    free_heap INT,
    uptime BIGINT,
    ip VARCHAR(50),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
