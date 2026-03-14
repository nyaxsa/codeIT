#include <iostream>
#include <fstream>
#include <string>
#include <limits>

using std::cin;
using std::cout;
using std::endl;
using std::string;

static int readMenuChoice(int minChoice, int maxChoice) {
  while (true) {
    cout << "Enter choice (" << minChoice << "-" << maxChoice << "): ";
    int choice;
    if (cin >> choice) {
      cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      if (choice >= minChoice && choice <= maxChoice) return choice;
      cout << "Invalid choice. Please enter a number between " << minChoice << " and " << maxChoice
           << "." << endl;
      continue;
    }

    cin.clear();
    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    cout << "Invalid input. Please enter a number." << endl;
  }
}

static string promptLine(const string& prompt) {
  cout << prompt;
  string line;
  std::getline(cin, line);
  return line;
}

static void createFile(const string& filename) {
  std::ofstream out(filename, std::ios::out | std::ios::trunc);
  if (!out) {
    cout << "Error: Could not create/open file: " << filename << endl;
    return;
  }
  cout << "File created (or cleared if it already existed): " << filename << endl;
}

static void writeFileOverwrite(const string& filename) {
  std::ofstream out(filename, std::ios::out | std::ios::trunc);
  if (!out) {
    cout << "Error: Could not open file for writing: " << filename << endl;
    return;
  }

  cout << "Enter text to write. Type a single line with END to finish." << endl;
  while (true) {
    string line = promptLine("> ");
    if (line == "END") break;
    out << line << '\n';
    if (!out) {
      cout << "Error: Write failed (disk/full/permission issue) while writing to: " << filename
           << endl;
      return;
    }
  }

  cout << "Successfully overwrote file: " << filename << endl;
}

static void appendToFile(const string& filename) {
  std::ofstream out(filename, std::ios::out | std::ios::app);
  if (!out) {
    cout << "Error: Could not open file for appending: " << filename << endl;
    return;
  }

  cout << "Enter text to append. Type a single line with END to finish." << endl;
  while (true) {
    string line = promptLine("> ");
    if (line == "END") break;
    out << line << '\n';
    if (!out) {
      cout << "Error: Append failed (disk/full/permission issue) while writing to: " << filename
           << endl;
      return;
    }
  }

  cout << "Successfully appended to file: " << filename << endl;
}

static void readFileAndDisplay(const string& filename) {
  std::ifstream in(filename);
  if (!in) {
    cout << "Error: Could not open file for reading: " << filename << endl;
    return;
  }

  cout << "----- File content: " << filename << " -----" << endl;
  string line;
  int lineNo = 1;
  bool any = false;
  while (std::getline(in, line)) {
    any = true;
    cout << lineNo++ << " | " << line << endl;
  }

  if (!in.eof() && in.fail()) {
    cout << "Error: Read failed while reading: " << filename << endl;
    return;
  }

  if (!any) cout << "(File is empty)" << endl;
  cout << "----- End of file -----" << endl;
}

static void showMenu() {
  cout << endl;
  cout << "==============================" << endl;
  cout << "   C++ File Management System " << endl;
  cout << "==============================" << endl;
  cout << "1. Create/Clear a text file" << endl;
  cout << "2. Write (overwrite) to a file" << endl;
  cout << "3. Append to a file" << endl;
  cout << "4. Read and display a file" << endl;
  cout << "5. Exit" << endl;
}

int main() {
  cout << "File Management System (fstream demo)" << endl;

  while (true) {
    showMenu();
    const int choice = readMenuChoice(1, 5);

    if (choice == 5) {
      cout << "Exiting. Goodbye!" << endl;
      return 0;
    }

    const string filename = promptLine("Enter filename (e.g., notes.txt): ");
    if (filename.empty()) {
      cout << "Invalid filename: cannot be empty." << endl;
      continue;
    }

    switch (choice) {
      case 1:
        createFile(filename);
        break;
      case 2:
        writeFileOverwrite(filename);
        break;
      case 3:
        appendToFile(filename);
        break;
      case 4:
        readFileAndDisplay(filename);
        break;
      default:
        cout << "Unexpected error: unknown menu choice." << endl;
        break;
    }
  }
}

