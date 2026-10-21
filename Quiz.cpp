
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <cctype>
#include <windows.h>

using namespace std;

// ================= CONFIGURATION =================

const char* COM_PORT = "\\\\.\\COM3";
const DWORD BAUD_RATE = CBR_115200;

const int MAX_STUDENTS = 100;
const int MAX_QUESTIONS = 100;

// ================= GLOBAL VARIABLES =================

HANDLE serialHandle = INVALID_HANDLE_VALUE;

int studentCount = 0;
int questionCount = 0;

vector<char> answerKey;
vector<int> scores;
vector<vector<char>> studentAnswers;

// ================= TEACHER INPUT =================

int getInteger(const string& message, int minimum, int maximum)
{
    string input;
    int value;

    while (true)
    {
        cout << message;
        getline(cin, input);

        stringstream ss(input);
        char extra;

        if ((ss >> value) && !(ss >> extra) &&
            value >= minimum && value <= maximum)
        {
            return value;
        }

        cout << "Invalid input. Enter a number from "
             << minimum << " to " << maximum << ".\n";
    }
}

char getCorrectAnswer(int questionNumber)
{
    string input;

    while (true)
    {
        cout << "Enter correct answer for Question "
             << questionNumber << " (A/B/C/D): ";

        getline(cin, input);

        if (input.size() == 1)
        {
            char answer = static_cast<char>(
                toupper(static_cast<unsigned char>(input[0]))
            );

            if (answer >= 'A' && answer <= 'D')
                return answer;
        }

        cout << "Invalid answer. Enter A, B, C, or D.\n";
    }
}

void teacherSetup()
{
    cout << "====================================\n";
    cout << "        CLASSROOM QUIZ SYSTEM\n";
    cout << "====================================\n\n";

    studentCount = getInteger(
        "Enter number of students (1-100): ",
        1, MAX_STUDENTS
    );

    questionCount = getInteger(
        "Enter number of questions (1-100): ",
        1, MAX_QUESTIONS
    );

    answerKey.clear();

    cout << "\nEnter the correct answers:\n";

    for (int q = 1; q <= questionCount; q++)
    {
        answerKey.push_back(getCorrectAnswer(q));
    }

    scores.assign(studentCount, 0);

    studentAnswers.assign(
        studentCount,
        vector<char>(questionCount, '-')
    );

    cout << "\nConfiguration completed.\n";
    cout << "Students  : " << studentCount << "\n";
    cout << "Questions : " << questionCount << "\n";

    cout << "\nMake sure the questions and options are displayed "
         << "on the projector separately.\n";

    cout << "Press Enter to connect to the ESP8266...";
    string unused;
    getline(cin, unused);
}

// ================= SERIAL PORT =================

bool openSerialPort()
{
    serialHandle = CreateFileA(
        COM_PORT,
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL
    );

    if (serialHandle == INVALID_HANDLE_VALUE)
    {
        cout << "ERROR: Cannot open " << COM_PORT << ".\n";
        cout << "Check the COM port and close Arduino Serial Monitor.\n";
        return false;
    }

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);

    if (!GetCommState(serialHandle, &dcb))
    {
        CloseHandle(serialHandle);
        serialHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    dcb.BaudRate = BAUD_RATE;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fBinary = TRUE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(serialHandle, &dcb))
    {
        CloseHandle(serialHandle);
        serialHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 50;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 1000;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(serialHandle, &timeouts))
    {
        CloseHandle(serialHandle);
        serialHandle = INVALID_HANDLE_VALUE;
        return false;
    }

    PurgeComm(serialHandle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    Sleep(2000);
    PurgeComm(serialHandle, PURGE_RXCLEAR);

    cout << "Serial port connected: COM3\n";
    return true;
}

void closeSerialPort()
{
    if (serialHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(serialHandle);
        serialHandle = INVALID_HANDLE_VALUE;
    }
}

// ================= SEND COMMAND =================

bool sendCommand(const string& command)
{
    string message = command + "\n";
    DWORD written = 0;

    return WriteFile(
        serialHandle,
        message.c_str(),
        static_cast<DWORD>(message.size()),
        &written,
        NULL
    ) && written == message.size();
}

// ================= READ SERIAL LINE =================


string readLine(DWORD timeoutMs)
{
    static string pending = "";

    DWORD start = GetTickCount();

    while (GetTickCount() - start < timeoutMs)
    {
        // Return a complete line if one is already buffered.
        size_t pos = pending.find('\n');

        if (pos != string::npos)
        {
            string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);

            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }

            return line;
        }

        char ch;
        DWORD bytesRead = 0;

        if (ReadFile(
                serialHandle,
                &ch,
                1,
                &bytesRead,
                NULL) && bytesRead == 1)
        {
            pending += ch;
        }
    }

    // Keep partial data for the next call.
    // Do not discard it when the timeout expires.
    return "";
}


bool waitForMessage(const string& expected, DWORD timeoutMs)
{
    DWORD start = GetTickCount();

    while (GetTickCount() - start < timeoutMs)
    {
        DWORD elapsed = GetTickCount() - start;
        string line = readLine(timeoutMs - elapsed);

        if (line.empty())
            continue;

        cout << "[ESP8266] " << line << "\n";

        if (line == expected)
            return true;
    }

    cout << "Timeout waiting for: " << expected << "\n";
    return false;
}

// ================= PARSE STUDENT RESULT =================
// Expected format: RESULT,studentID,questionNumber,answer

bool parseResult(
    const string& line,
    int& studentID,
    int& questionNumber,
    char& answer)
{
    stringstream ss(line);
    string prefix, idText, questionText, answerText;

    if (!getline(ss, prefix, ',') ||
        !getline(ss, idText, ',') ||
        !getline(ss, questionText, ',') ||
        !getline(ss, answerText))
    {
        return false;
    }

    if (prefix != "RESULT" || answerText.size() != 1)
        return false;

    try
    {
        size_t pos1 = 0, pos2 = 0;

        studentID = stoi(idText, &pos1);
        questionNumber = stoi(questionText, &pos2);

        if (pos1 != idText.size() || pos2 != questionText.size())
            return false;
    }
    catch (...)
    {
        return false;
    }

    answer = static_cast<char>(
        toupper(static_cast<unsigned char>(answerText[0]))
    );

    return answer >= 'A' && answer <= 'D';
}

// ================= CONFIGURE ESP8266 =================

bool configureESP8266()
{
    cout << "\nSending student count...\n";

    if (!sendCommand("STUDENTS," + to_string(studentCount)))
        return false;

    if (!waitForMessage(
            "ACK,STUDENTS," + to_string(studentCount), 10000))
        return false;

    cout << "\nSending question count...\n";

    if (!sendCommand("QUESTIONS," + to_string(questionCount)))
        return false;

    if (!waitForMessage(
            "ACK,QUESTIONS," + to_string(questionCount), 10000))
        return false;

    return true;
}

// ================= COLLECT ANSWERS =================

bool collectAnswers(int questionNumber)
{
    vector<bool> answered(studentCount, false);
    int answeredCount = 0;

    const DWORD QUESTION_TIMEOUT = 300000;
    DWORD start = GetTickCount();

    cout << "\n====================================\n";
    cout << "Question " << questionNumber
         << " of " << questionCount << "\n";
    cout << "====================================\n";
    cout << "The question and options should be shown "
         << "on the projector.\n";
    cout << "Waiting for student responses...\n\n";

    while (answeredCount < studentCount)
    {
        if (GetTickCount() - start >= QUESTION_TIMEOUT)
        {
            cout << "\nTimeout: received " << answeredCount
                 << " of " << studentCount << " answers.\n";
            return false;
        }

        string line = readLine(1000);

        if (line.empty())
            continue;

        if (line == "ALL_ANSWERED")
            continue;

        int id = 0;
        int qNumber = 0;
        char answer = '-';

        if (!parseResult(line, id, qNumber, answer))
        {
            cout << "[ESP8266] " << line << "\n";
            continue;
        }

        if (qNumber != questionNumber)
            continue;

        if (id < 1 || id > studentCount)
        {
            cout << "Invalid student ID: " << id << "\n";
            continue;
        }

        int index = id - 1;

        if (answered[index])
        {
            cout << "Duplicate answer from Student "
                 << id << " ignored.\n";
            continue;
        }

        answered[index] = true;
        answeredCount++;

        studentAnswers[index][questionNumber - 1] = answer;

        if (answer == answerKey[questionNumber - 1])
            scores[index]++;

        cout << "Student " << id
             << " answered " << answer
             << " | " << answeredCount
             << "/" << studentCount << " received\n";
    }

    if (!waitForMessage("ALL_ANSWERED", 5000))
    {
        cout << "All individual answers were received; "
             << "completion notification was not received.\n";
    }

    cout << "\nAll students have answered Question "
         << questionNumber << ".\n";

    return true;
}

// ================= EXPORT CSV =================

bool exportResults()
{
    ofstream file("QuizResults.csv");

    if (!file.is_open())
    {
        cout << "Cannot create QuizResults.csv.\n";
        return false;
    }

    file << "Student ID";

    for (int q = 0; q < questionCount; q++)
    {
        file << ",Q" << q + 1 << " Answer";
        file << ",Q" << q + 1 << " Marks";
    }

    file << ",Total Marks\n";

    for (int s = 0; s < studentCount; s++)
    {
        file << s + 1;

        for (int q = 0; q < questionCount; q++)
        {
            char answer = studentAnswers[s][q];

            file << "," << answer;
            file << "," << (answer == answerKey[q] ? 1 : 0);
        }

        file << "," << scores[s] << "\n";
    }

    file.close();

    cout << "\nResults saved to QuizResults.csv\n";
    return true;
}

// ================= DISPLAY RESULTS =================

void displayResults()
{
    cout << "\n====================================\n";
    cout << "           FINAL RESULTS\n";
    cout << "====================================\n";

    cout << "Student ID\tScore\tTotal\n";

    for (int i = 0; i < studentCount; i++)
    {
        cout << i + 1 << "\t\t"
             << scores[i] << "\t"
             << questionCount << "\n";
    }
}

// ================= MAIN =================

int main()
{
    teacherSetup();

    if (!openSerialPort())
        return 1;

    if (!configureESP8266())
    {
        cout << "ESP8266 configuration failed.\n";
        closeSerialPort();
        return 1;
    }

    for (int q = 1; q <= questionCount; q++)
    {
        cout << "\nStarting question " << q << "...\n";

        if (!sendCommand("Q," + to_string(q)))
        {
            cout << "Failed to send question command.\n";
            closeSerialPort();
            return 1;
        }

        if (!waitForMessage("ACK," + to_string(q), 10000))
        {
            cout << "ESP8266 did not acknowledge the question.\n";
            closeSerialPort();
            return 1;
        }

        if (!collectAnswers(q))
        {
            cout << "Quiz stopped because some answers are missing.\n";
            closeSerialPort();
            return 1;
        }

        if (q < questionCount)
            Sleep(1500);
    }

    closeSerialPort();

    displayResults();
    exportResults();

    cout << "\nQuiz completed.\n";
    cout << "Press Enter to exit...";
    string unused;
    getline(cin, unused);

    return 0;
}
