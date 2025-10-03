<?php
header("Content-Type: application/json");

// Ví dụ: trả về lệnh "feed"
$command = [
    "command" => "feed",
    "amount"  => 0.5 // nửa ký
];

// Bạn có thể đổi thành lệnh khác, ví dụ:
// $command = ["command" => "get_logs"];
// $command = ["command" => "get_schedule"];
// $command = ["command" => "update_schedule", "schedule" => "08:00,12:00,18:00"];
// $command = ["command" => "delete_log"];

echo json_encode($command);
