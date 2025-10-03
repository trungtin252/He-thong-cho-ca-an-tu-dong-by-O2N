<?php
header("Content-Type: application/json");
require "config.php";

$raw = file_get_contents("php://input");
$data = json_decode($raw, true);

if (!$data) {
    echo json_encode(["status" => "error", "message" => "Invalid JSON"]);
    exit;
}

$type = $data['type'] ?? '';

if ($type === 'logs') {
    $date = $data['date'] ?? '';
    $logs = $data['data'] ?? '';

    $stmt = $conn->prepare("INSERT INTO logs (log_date, data) VALUES (?, ?)");
    $stmt->bind_param("ss", $date, $logs);
    $stmt->execute();

    echo json_encode(["status" => "ok", "message" => "Logs saved"]);
}

elseif ($type === 'schedule') {
    $schedule = implode(",", $data['data'] ?? []);

    $stmt = $conn->prepare("INSERT INTO schedules (schedule_data) VALUES (?)");
    $stmt->bind_param("s", $schedule);
    $stmt->execute();

    echo json_encode(["status" => "ok", "message" => "Schedule saved"]);
}

elseif ($type === 'status') {
    $wifi   = $data['wifi_rssi'] ?? 0;
    $heap   = $data['free_heap'] ?? 0;
    $uptime = $data['uptime'] ?? 0;
    $ip     = $data['ip'] ?? '';

    $stmt = $conn->prepare("INSERT INTO status (wifi_rssi, free_heap, uptime, ip) VALUES (?,?,?,?)");
    $stmt->bind_param("iiis", $wifi, $heap, $uptime, $ip);
    $stmt->execute();

    echo json_encode(["status" => "ok", "message" => "Status saved"]);
}

elseif ($type === 'heartbeat') {
    echo json_encode(["status" => "ok", "message" => "Heartbeat received"]);
}

else {
    echo json_encode(["status" => "error", "message" => "Unknown type"]);
}
?>
