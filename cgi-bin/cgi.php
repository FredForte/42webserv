<!DOCTYPE html>
<html>
<body>

<h1>PHP Script! — Printing environment variables:</h1>

<?php
foreach (getenv() as $key => $value) {
    echo "<p>$key: $value</p>\n";
}
?>

</body>
</html>
