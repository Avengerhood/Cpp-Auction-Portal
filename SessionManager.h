#include <unordered_map>
#include <chrono>
#include <random>
#include <sstream>

using namespace std;

class SessionManager {
private:
    const string SECRET_KEY = "K&K";  // Klucz u¿ywany do podpisu ciasteczka
    unordered_map<string, uint32_t> sessions;  // Mapa: Token sesji -> ID u¿ytkownika -- zaszyfrowany token
    unordered_map<string, uint32_t> userID;  // Mapa: Token sesji -> ID u¿ytkownika -- tylko ID w tokenie
    unordered_map<string, uint32_t> userRoles;  // Mapa: Token sesji -> Rola u¿ytkownika
    unordered_map<uint32_t, string> userSession;
    unordered_map<string, string> userEmails;  // Mapa: Token sesji -> Email u¿ytkownika
    unordered_map<string, string> userFirstnames;  // Mapa: Token sesji -> Imiê u¿ytkownika
    unordered_map<string, string> userLastnames;  // Mapa: Token sesji -> Nazwisko u¿ytkownika
    unordered_map<string, string> userPassword;  // Mapa: Token sesji -> Has³o U¿ytkownika
    unordered_map<string, string> userPhoneNumber;  // Mapa: Token sesji -> Numer Telefonu U¿ytkownika
    unordered_map<string, string> userLogin;  // Mapa: Token sesji -> Login U¿ytkownika
    unordered_map<string, string> userAddress;  // Mapa: Token sesji -> Adres U¿ytkownika
    unordered_map<string, string> userCity;  // Mapa: Token sesji -> Poczta U¿ytkownika
    unordered_map<string, string> userZip;  // Mapa: Token sesji -> Kod Pocztowy U¿ytkownika


    string generate_session_token() {
        auto now = chrono::system_clock::now().time_since_epoch().count();
        random_device rd;
        mt19937 generator(rd());
        uniform_int_distribution<> distribution(0, 999999);

        stringstream ss;
        ss << now << distribution(generator);
        return ss.str();
    }

    string generate_signature(const string& data) {
        // W rzeczywistoœci zaleca siê u¿ywanie silniejszej funkcji hashuj¹cej, np. HMAC
        size_t hashedValue = std::hash<std::string>{}(data + SECRET_KEY);
        return std::to_string(hashedValue);
    }

    bool validate_signature(const string& data, const string& signature) {
        return generate_signature(data) == signature;
    }

public:
    // Tworzy now¹ sesjê dla danego u¿ytkownika i zwraca token sesji
    string create_session(uint32_t user_id) {
        string token = generate_session_token();
        sessions[token] = user_id;
        return token + "." + generate_signature(token);
    }

    bool validate_session(const string& token_with_signature) {
        auto separator_pos = token_with_signature.find('.');
        if (separator_pos == string::npos) return false;

        string token = token_with_signature.substr(0, separator_pos);
        string signature = token_with_signature.substr(separator_pos + 1);

        if (validate_signature(token, signature) && sessions.find(token) != sessions.end()) {
            return true;
        }
        return false;
    }

    // Usuwa dan¹ sesjê (wylogowuje u¿ytkownika)
    void invalidate_session(const string& token) {
        sessions.erase(token);
    }

    // Pobiera ID u¿ytkownika z danego tokenu sesji
    uint32_t get_user_id(const string& token) {
        auto it = userID.find(token);
        if (it != userID.end()) {
            return it->second;
        }
        return 0;  // Zwraca 0, jeœli sesja nie istnieje
    }


    // Ustawia rolê dla danego tokenu sesji
    void set_user_role(const string& token, uint32_t role) {
        userRoles[token] = role;
    }

    // Pobiera rolê u¿ytkownika z danego tokenu sesji
    uint32_t get_user_role(const string& token) const {
        auto it = userRoles.find(token);
        if (it != userRoles.end()) {
            return it->second;
        }
        return 0;  // Zwraca 0, jeœli sesja nie istnieje
    }

    void set_user_email(const string& token, const string& email) {
        userEmails[token] = email;
    }

    string get_user_email(const string& token) const {
        auto it = userEmails.find(token);
        if (it != userEmails.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    void set_user_firstname(const string& token, const string& firstname) {
        userFirstnames[token] = firstname;
    }

    string get_user_firstname(const string& token) const {
        auto it = userFirstnames.find(token);
        if (it != userFirstnames.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    void set_user_lastname(const string& token, const string& lastname) {
        userLastnames[token] = lastname;
    }

    string get_user_lastname(const string& token) const {
        auto it = userLastnames.find(token);
        if (it != userLastnames.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    void set_user_password(const string& token, const string& password) {
        userPassword[token] = password;
    }

    string get_user_password(const string& token) const {
        auto it = userPassword.find(token);
        if (it != userPassword.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    void set_user_phonenumber(const string& token, const string& phonenumber) {
        userPhoneNumber[token] = phonenumber;
    }

    string get_user_phonenumber(const string& token) const {
        auto it = userPhoneNumber.find(token);
        if (it != userPhoneNumber.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    void set_user_login(const string& token, const string& login) {
        userLogin[token] = login;
    }

    string get_user_login(const string& token) const {
        auto it = userLogin.find(token);
        if (it != userLogin.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    void set_user_address(const string& token, const string& address) {
        userAddress[token] = address;
    }

    string get_user_address(const string& token) const {
        auto it = userAddress.find(token);
        if (it != userAddress.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    void set_user_city(const string& token, const string& city) {
        userCity[token] = city;
    }

    string get_user_city(const string& token) const {
        auto it = userCity.find(token);
        if (it != userCity.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    void set_user_zip(const string& token, const string& zip) {
        userZip[token] = zip;
    }

    string get_user_zip(const string& token) const {
        auto it = userZip.find(token);
        if (it != userZip.end()) {
            return it->second;
        }
        return "";  // Zwraca pusty string, jeœli sesja nie istnieje
    }

    // Ustawia ID dla danego tokenu sesji
    void set_user_id(const string& token, uint32_t id) {
        userID[token] = id;
    }

    void set_user_session(const uint32_t id, string& token) {
        userSession[id] = token;
    }

    vector<string> get_user_session(const vector<int>& ids) {
        std::vector<std::string> sessions;
        for (auto id : ids) {
            auto it = userSession.find(id);
            if (it != userSession.end()) {
                sessions.push_back(it->second);
            }
        }
        return sessions;
    }


};

