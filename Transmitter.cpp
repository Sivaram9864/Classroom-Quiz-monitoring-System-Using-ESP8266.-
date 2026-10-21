
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* ssid = "QuizNetwork";
const char* password = "QuizPass123";

ESP8266WebServer server(80);

const int MAX_STUDENTS = 100;

int totalStudents = 5;
int totalQuestions = 5;
int currentQuestion = 1;

String answeredStudents[MAX_STUDENTS];
int answeredCount = 0;

// Validate a numeric Student ID from 1 to totalStudents
bool isValidStudentID(String id)
{
    if (id.length() == 0)
        return false;

    for (unsigned int i = 0; i < id.length(); i++) {
        if (id[i] < '0' || id[i] > '9')
            return false;
    }

    int value = id.toInt();

    if (String(value) != id)
        return false;

    return value >= 1 && value <= totalStudents;
}

void resetQuestionAnswers()
{
    answeredCount = 0;

    for (int i = 0; i < MAX_STUDENTS; i++) {
        answeredStudents[i] = "";
    }
}

// Main webpage
void handleRoot()
{
    String page = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Classroom Quiz</title>
<style>
body {
    font-family: Arial;
    text-align: center;
    margin: 25px;
}
input {
    padding: 12px;
    font-size: 18px;
    width: 80%;
    max-width: 300px;
    margin: 10px;
}
button {
    padding: 15px 25px;
    margin: 8px;
    font-size: 20px;
    cursor: pointer;
}
</style>
</head>
<body>
<h2>Classroom Quiz</h2>
<h3>Question: <span id="qno">Loading...</span></h3>
<p>Enter your Student ID</p>
<input type="text" id="studentID" maxlength="10"
       placeholder="Student ID">

<p>Select your answer:</p>
<button onclick="submitAnswer('A')">A</button>
<button onclick="submitAnswer('B')">B</button>
<button onclick="submitAnswer('C')">C</button>
<button onclick="submitAnswer('D')">D</button>

<p id="message"></p>

<script>
async function updateQuestion() {
    try {
        const r = await fetch('/status');
        const d = await r.json();
        document.getElementById('qno').textContent =
            d.question + ' of ' + d.totalQuestions;
    } catch(e) {
        document.getElementById('qno').textContent =
            'Unavailable';
    }
}

async function submitAnswer(choice) {
    const studentID =
        document.getElementById('studentID').value.trim();

    if (!studentID) {
        document.getElementById('message').textContent =
            'Enter your Student ID.';
        return;
    }

    const params = new URLSearchParams({
        student: studentID,
        choice: choice
    });

    try {
        const r = await fetch('/answer?' + params);
        document.getElementById('message').textContent =
            await r.text();
    } catch(e) {
        document.getElementById('message').textContent =
            'Submission failed. Try again.';
    }
}

updateQuestion();
setInterval(updateQuestion, 1000);
</script>
</body>
</html>
)rawliteral";

    server.send(200, "text/html", page);
}

void handleStatus()
{
    String json = "{\"question\":" +
                  String(currentQuestion) +
                  ",\"totalQuestions\":" +
                  String(totalQuestions) + "}";

    server.send(200, "application/json", json);
}

void handleAnswer()
{
    if (!server.hasArg("student") ||
        !server.hasArg("choice")) {
        server.send(400, "text/plain",
                    "Missing Student ID or answer.");
        return;
    }

    String studentID = server.arg("student");
    String answer = server.arg("choice");

    studentID.trim();
    answer.trim();
    answer.toUpperCase();

    // Silently reject invalid IDs: no serial result or count
    if (!isValidStudentID(studentID)) {
        server.send(400, "text/plain",
                    "Invalid Student ID.");
        return;
    }

    if (answer.length() != 1 ||
        answer[0] < 'A' || answer[0] > 'D') {
        server.send(400, "text/plain",
                    "Select A, B, C, or D.");
        return;
    }

    if (answeredCount >= totalStudents) {
        server.send(200, "text/plain",
                    "All students have answered.");
        return;
    }

    // One answer per student per question
    for (int i = 0; i < answeredCount; i++) {
        if (answeredStudents[i] == studentID) {
            server.send(200, "text/plain",
                        "You have already answered.");
            return;
        }
    }

    answeredStudents[answeredCount] = studentID;
    answeredCount++;

    // Preserve the PC serial protocol
    Serial.print("RESULT,");
    Serial.print(studentID);
    Serial.print(",");
    Serial.print(currentQuestion);
    Serial.print(",");
    Serial.println(answer);

    server.send(200, "text/plain",
                "Answer submitted successfully.");

    if (answeredCount == totalStudents) {
        Serial.println("ALL_ANSWERED");
    }
}

void processCommand(String command)
{
    command.trim();

    if (command.startsWith("STUDENTS,")) {
        int n = command.substring(9).toInt();

        if (n >= 1 && n <= MAX_STUDENTS) {
            totalStudents = n;
            resetQuestionAnswers();

            Serial.print("ACK,STUDENTS,");
            Serial.println(totalStudents);
        }
        return;
    }

    if (command.startsWith("QUESTIONS,")) {
        int n = command.substring(10).toInt();

        if (n >= 1 && n <= 100) {
            totalQuestions = n;

            if (currentQuestion > totalQuestions)
                currentQuestion = 1;

            resetQuestionAnswers();

            Serial.print("ACK,QUESTIONS,");
            Serial.println(totalQuestions);
        }
        return;
    }

    if (command.startsWith("Q,")) {
        int q = command.substring(2).toInt();

        if (q >= 1 && q <= totalQuestions) {
            currentQuestion = q;
            resetQuestionAnswers();

            Serial.print("ACK,");
            Serial.println(currentQuestion);
        }
    }
}

void setup()
{
    Serial.begin(115200);
    Serial.setTimeout(50);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, password);

    server.on("/", HTTP_GET, handleRoot);
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/answer", HTTP_GET, handleAnswer);

    server.begin();

    Serial.println();
    Serial.println("ESP8266 Quiz Controller Ready");
    Serial.print("Wi-Fi: ");
    Serial.println(ssid);
    Serial.print("Webpage: http://");
    Serial.println(WiFi.softAPIP());
}

void loop()
{
    server.handleClient();

    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        processCommand(command);
    }
}
