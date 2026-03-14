#include <iostream>
#include <vector>
#include <stack>
#include <cctype>
#include <stdexcept>
#include <string>

using namespace std;

// -------- Token definitions --------

enum class TokenType {
    NUMBER,
    OPERATOR,
    LEFT_PAREN,
    RIGHT_PAREN
};

struct Token {
    TokenType type;
    string text; // original text representation
    int value;   // used only for NUMBER tokens
};

// -------- Function declarations --------

vector<Token> tokenize(const string &expr);
int precedence(char op);
bool isOperator(char c);
bool isLeftAssociative(char op);
vector<Token> parseToPostfix(const vector<Token> &tokens);
int evaluatePostfix(const vector<Token> &postfix);

// -------- Helper printing functions --------

void printTokens(const vector<Token> &tokens) {
    cout << "Tokens: [";
    for (size_t i = 0; i < tokens.size(); ++i) {
        cout << tokens[i].text;
        if (i + 1 < tokens.size()) {
            cout << ", ";
        }
    }
    cout << "]" << endl;
}

void printPostfix(const vector<Token> &postfix) {
    cout << "Postfix (parsed expression): ";
    for (const auto &tok : postfix) {
        cout << tok.text << " ";
    }
    cout << endl;
}

// -------- Main --------

int main() {
    cout << "Simple Arithmetic Expression Compiler/Evaluator" << endl;
    cout << "Supports +, -, *, / and parentheses ()." << endl;
    cout << "Enter an expression, or 'q' to quit." << endl;

    while (true) {
        cout << "\nExpression: ";
        string line;
        if (!getline(cin, line)) {
            break;
        }

        if (line == "q" || line == "Q") {
            break;
        }

        try {
            // Lexical analysis
            vector<Token> tokens = tokenize(line);
            printTokens(tokens);

            // Syntax parsing (Shunting Yard -> postfix)
            vector<Token> postfix = parseToPostfix(tokens);
            printPostfix(postfix);

            // Evaluation
            int result = evaluatePostfix(postfix);
            cout << "Result: " << result << endl;
        } catch (const exception &ex) {
            cout << "Error: " << ex.what() << endl;
        }
    }

    cout << "Goodbye!" << endl;
    return 0;
}

// -------- Lexical Analysis --------

vector<Token> tokenize(const string &expr) {
    vector<Token> tokens;
    size_t i = 0;

    while (i < expr.size()) {
        char c = expr[i];

        if (isspace(static_cast<unsigned char>(c))) {
            // Skip whitespace
            ++i;
            continue;
        }

        if (isdigit(static_cast<unsigned char>(c))) {
            // Parse a multi-digit integer
            int value = 0;
            size_t start = i;
            while (i < expr.size() && isdigit(static_cast<unsigned char>(expr[i]))) {
                value = value * 10 + (expr[i] - '0');
                ++i;
            }
            Token tok;
            tok.type = TokenType::NUMBER;
            tok.value = value;
            tok.text = expr.substr(start, i - start);
            tokens.push_back(tok);
        } else if (isOperator(c)) {
            Token tok;
            tok.type = TokenType::OPERATOR;
            tok.text = string(1, c);
            tok.value = 0;
            tokens.push_back(tok);
            ++i;
        } else if (c == '(') {
            Token tok;
            tok.type = TokenType::LEFT_PAREN;
            tok.text = "(";
            tok.value = 0;
            tokens.push_back(tok);
            ++i;
        } else if (c == ')') {
            Token tok;
            tok.type = TokenType::RIGHT_PAREN;
            tok.text = ")";
            tok.value = 0;
            tokens.push_back(tok);
            ++i;
        } else {
            throw runtime_error(string("Invalid character in expression: '") + c + "'");
        }
    }

    if (tokens.empty()) {
        throw runtime_error("Empty expression.");
    }

    return tokens;
}

bool isOperator(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/';
}

int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    return 0;
}

bool isLeftAssociative(char op) {
    // All our operators (+, -, *, /) are left-associative
    return op == '+' || op == '-' || op == '*' || op == '/';
}

// -------- Parsing: Shunting Yard to postfix --------

vector<Token> parseToPostfix(const vector<Token> &tokens) {
    vector<Token> output;
    stack<Token> opStack;

    for (const auto &tok : tokens) {
        if (tok.type == TokenType::NUMBER) {
            output.push_back(tok);
        } else if (tok.type == TokenType::OPERATOR) {
            char op1 = tok.text[0];
            while (!opStack.empty() && opStack.top().type == TokenType::OPERATOR) {
                char op2 = opStack.top().text[0];
                if ((isLeftAssociative(op1) && precedence(op1) <= precedence(op2)) ||
                    (!isLeftAssociative(op1) && precedence(op1) < precedence(op2))) {
                    output.push_back(opStack.top());
                    opStack.pop();
                } else {
                    break;
                }
            }
            opStack.push(tok);
        } else if (tok.type == TokenType::LEFT_PAREN) {
            opStack.push(tok);
        } else if (tok.type == TokenType::RIGHT_PAREN) {
            bool foundLeftParen = false;
            while (!opStack.empty()) {
                if (opStack.top().type == TokenType::LEFT_PAREN) {
                    foundLeftParen = true;
                    opStack.pop();
                    break;
                } else {
                    output.push_back(opStack.top());
                    opStack.pop();
                }
            }
            if (!foundLeftParen) {
                throw runtime_error("Mismatched parentheses.");
            }
        }
    }

    // Pop any remaining operators
    while (!opStack.empty()) {
        if (opStack.top().type == TokenType::LEFT_PAREN ||
            opStack.top().type == TokenType::RIGHT_PAREN) {
            throw runtime_error("Mismatched parentheses.");
        }
        output.push_back(opStack.top());
        opStack.pop();
    }

    if (output.empty()) {
        throw runtime_error("Invalid expression.");
    }

    return output;
}

// -------- Evaluation of postfix expression --------

int evaluatePostfix(const vector<Token> &postfix) {
    stack<int> st;

    for (const auto &tok : postfix) {
        if (tok.type == TokenType::NUMBER) {
            st.push(tok.value);
        } else if (tok.type == TokenType::OPERATOR) {
            if (st.size() < 2) {
                throw runtime_error("Not enough operands for operator '" + tok.text + "'.");
            }
            int right = st.top(); st.pop();
            int left  = st.top(); st.pop();
            char op = tok.text[0];
            int result = 0;

            switch (op) {
                case '+':
                    result = left + right;
                    break;
                case '-':
                    result = left - right;
                    break;
                case '*':
                    result = left * right;
                    break;
                case '/':
                    if (right == 0) {
                        throw runtime_error("Division by zero.");
                    }
                    result = left / right;
                    break;
                default:
                    throw runtime_error("Unknown operator: " + tok.text);
            }

            st.push(result);
        } else {
            throw runtime_error("Unexpected token in postfix expression: " + tok.text);
        }
    }

    if (st.size() != 1) {
        throw runtime_error("Invalid postfix expression.");
    }

    return st.top();
}

