#include "crow.h"
#include "Database.h"
#include "SessionManager.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>

using namespace std;
using namespace crow;


class RoleManager {
public:
    static const uint32_t REGULAR_USER = 0;
    static const uint32_t ADMIN = 2;

    // Weryfikuje, czy dany token sesji odpowiada u¿ytkownikowi z rol¹ ADMIN
    static bool is_admin(const SessionManager& sessionManager, const string& token) {
        return sessionManager.get_user_role(token) == ADMIN;
    }
};


class WebApp {
private:
    SimpleApp app;
    Database db;
    SessionManager sessionManager;
    RoleManager roleManager;

    string urlDecode(const string& encoded) {
        string decoded;
        decoded.reserve(encoded.length());

        for (size_t i = 0; i < encoded.length(); ++i) {
            if (encoded[i] == '%' && i + 2 < encoded.length()) {
                string hex = encoded.substr(i + 1, 2);
                stringstream hexStream(hex);
                int intValue;
                if (hexStream >> hex >> intValue) {
                    decoded += static_cast<char>(intValue);
                    i += 2;
                }
                else {
                    decoded += '%';
                }
            }
            else if (encoded[i] == '+') {
                decoded += ' ';
            }
            else {
                decoded += encoded[i];
            }
        }

        return decoded;
    }

    response sendFile(const string& filename) {
        ifstream file(filename, ios::binary);
        if (file) {
            ostringstream ss;
            ss << file.rdbuf();
            file.close();

            string extension = filename.substr(filename.find_last_of(".") + 1);

            string mimeType = "text/plain";
            if (extension == "css") {
                mimeType = "text/css";
            }
            else if (extension == "js") {
                mimeType = "application/javascript";
            }
            else if (extension == "png") {
                mimeType = "image/png";
            }
            else if (extension == "jpg" || extension == "jpeg") {
                mimeType = "image/jpeg";
            }
            else if (extension == "gif") {
                mimeType = "image/gif";
            }
            else if (extension == "svg") {
                mimeType = "image/svg+xml";
            }

            return response(200, ss.str(), mimeType);
        }
        return response(404);
    }


    unordered_map<string, string> parse_form_data(const string& body) {
        unordered_map<string, string> data;
        string::size_type pos = 0;
        while (pos < body.size()) {
            auto key_pos = pos;
            auto key_end = body.find('=', pos);
            if (key_end == string::npos) {
                break;
            }
            auto val_pos = key_end + 1;
            auto val_end = body.find('&', pos);
            if (val_end == string::npos) {
                val_end = body.size();
            }
            string key = body.substr(key_pos, key_end - key_pos);
            string value = body.substr(val_pos, val_end - val_pos);
            data[key] = value;
            pos = val_end + 1;
        }
        return data;
    }

    


    string get_cookie_value(const request& req, const string& cookie_name) {
        auto it = req.headers.find("Cookie");
        if (it != req.headers.end()) {
            string cookies = it->second;
            string name = cookie_name + "=";
            size_t start_pos = cookies.find(name);
            if (start_pos != string::npos) {
                start_pos += name.length();
                size_t end_pos = cookies.find(";", start_pos);
                string value = cookies.substr(start_pos, end_pos - start_pos);
                cout << "Znalezione ciasteczko: " << value << endl;
                return value;
            }
        }
        cout << "Ciasteczko nie znalezione." << endl;
        return "";
    }

    response respond_with_json(int status, const string& json_str) {
        response resp(status, json_str);
        resp.set_header("Content-Type", "application/json; charset=utf-8");
        return resp;
    }




    string url_decode(const string& str) {
        string decoded;
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '%' && i + 2 < str.length()) {
                int value;
                istringstream is(str.substr(i + 1, 2));
                if (is >> hex >> value) {
                    decoded += static_cast<char>(value);
                    i += 2;
                }
                else {
                    decoded += str[i];
                }
            }
            else if (str[i] == '+') {
                decoded += ' ';
            }
            else {
                decoded += str[i];
            }
        }
        return decoded;
    }

public:
    WebApp() : db("snpp") {

        CROW_ROUTE(app, "/static/<string>")
            .methods(HTTPMethod::Get)
            ([this](const request& req, string filename) {
            string decodedFilename = urlDecode(filename);
            auto path = "static/" + decodedFilename;

            cout << "Trying to send file: " << path << endl;

            return this->sendFile(path);
                });


        CROW_ROUTE(app, "/")
            ([this](const request& req) {
            mustache::context ctx;
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            bool is_logged_in = !sessionHash.empty() && this->sessionManager.validate_session(sessionHash);
            auto user_id = this->sessionManager.get_user_id(sessionHash);
            if (user_id == 0 || !this->db.isUserExist(user_id)) {
                is_logged_in = false;
            }
            ctx["logged_in"] = is_logged_in; // Dodanie informacji o logowaniu do kontekstu

            if (is_logged_in) {
                // Pobranie danych u¿ytkownika, gdy jest zalogowany
                string firstname = this->sessionManager.get_user_firstname(sessionHash);
                string lastname = this->sessionManager.get_user_lastname(sessionHash);
                string email = this->sessionManager.get_user_email(sessionHash);
                uint32_t role = this->sessionManager.get_user_role(sessionHash);

                // Wype³nienie kontekstu szablonu danymi u¿ytkownika
                ctx["username"] = firstname + " " + lastname;
                ctx["email"] = email;
                ctx["is_admin"] = (role == 2);
            }
            else {
                response resp(mustache::load("home/index.html").render(ctx));
                resp.set_header("Set-Cookie", "X-Session-Hash=deleted; Path=/; HttpOnly; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
                return resp;
            }



            // Renderowanie strony g³ównej z odpowiednim kontekstem
            return response(mustache::load("home/index.html").render(ctx));
                });


        CROW_ROUTE(app, "/register")
            .methods(HTTPMethod::Get)
            ([]() {
            mustache::context ctx;
            return mustache::load("home/register.html").render();
                });

        CROW_ROUTE(app, "/register")
            .methods(HTTPMethod::Post)
            ([this](const request& req) {
            cout << "Przyjêto request." << endl;
            auto form_data = this->parse_form_data(req.body);

            auto email_iter = form_data.find("email");
            auto password_iter = form_data.find("password");
            auto firstname_iter = form_data.find("firstname");
            auto lastname_iter = form_data.find("lastname");
            auto phone_number_iter = form_data.find("phone_number");
            auto login_iter = form_data.find("login");
            auto address_iter = form_data.find("address");
            auto city_iter = form_data.find("city");
            auto zip_iter = form_data.find("zip");

            if (email_iter == form_data.end() || password_iter == form_data.end() || firstname_iter == form_data.end() || lastname_iter == form_data.end() || phone_number_iter == form_data.end() || login_iter == form_data.end() || address_iter == form_data.end() || city_iter == form_data.end() || zip_iter == form_data.end()) {
                return respond_with_json(400, u8"{\"error\": \"Brak wymaganych danych!\"}");
            }

            string email = url_decode(email_iter->second);
            string password = password_iter->second;
            string firstname = url_decode(firstname_iter->second);
            string lastname = url_decode(lastname_iter->second);
            string phone_number = url_decode(phone_number_iter->second);
            string login = url_decode(login_iter->second);
            string address = url_decode(address_iter->second);
            string city = url_decode(city_iter->second);
            string zip = url_decode(zip_iter->second);

            // Sprawdzanie, czy adres email jest ju¿ wykorzystany
            if (this->db.doesUserExist(email, "email")) {
                return respond_with_json(200, u8"{\"error\": \"E-Mail zosta³ ju¿ wykorzystany!\"}");
            }

            // Sprawdzanie, czy login jest ju¿ wykorzystany
            if (this->db.doesUserExist(login, "login")) {
                return respond_with_json(200, u8"{\"error\": \"Login zosta³ ju¿ wykorzystany!\"}");
            }

            // Dodawanie u¿ytkownika
            if (this->db.addUser(firstname, lastname, email, phone_number, login, password, address, city, zip)) {
                return respond_with_json(200, u8"{\"success\": \"Zarejestrowano pomyœlnie\"}");
            }
            else {
                return respond_with_json(200, u8"{\"error\": \"Nie uda³o siê zarejestrowaæ u¿ytkownika\"}");
            }

                });

        CROW_ROUTE(app, "/login")
            .methods(HTTPMethod::Get)
            ([]() {
            mustache::context ctx;
            return mustache::load("home/login.html").render();
                });

        CROW_ROUTE(app, "/login")
            .methods(HTTPMethod::Post)
            ([this](const request& req) {
            auto x = json::load(req.body);
            if (!x) {
                return response(200, u8R"({"status": "error", "message": "Nieprawid³owe dane JSON."})");
            }

            string login = x["login"].s();
            string password = x["password"].s();

            
            auto loginStatus = this->db.checkCredentials(login, password);

            int userId = this->db.getUserIdByLogin(login);

            switch (loginStatus) {
            case Database::LoginStatus::Success: {
                // Jeœli uwierzytelnienie jest poprawne, utwórz sesjê.
                string sessionHash = this->sessionManager.create_session(userId);
                Database::UserData userData = this->db.getUserData(login);
                this->sessionManager.set_user_id(sessionHash, userData.id);
                this->sessionManager.set_user_role(sessionHash, userData.role);
                this->sessionManager.set_user_firstname(sessionHash, userData.firstname);
                this->sessionManager.set_user_lastname(sessionHash, userData.lastname);
                this->sessionManager.set_user_email(sessionHash, userData.email);
                this->sessionManager.set_user_login(sessionHash, userData.login);
                this->sessionManager.set_user_password(sessionHash, userData.password);
                this->sessionManager.set_user_phonenumber(sessionHash, userData.phone_number);
                this->sessionManager.set_user_address(sessionHash, userData.address);
                this->sessionManager.set_user_city(sessionHash, userData.city);
                this->sessionManager.set_user_zip(sessionHash, userData.zip);
                this->sessionManager.set_user_session(userData.id, sessionHash);

                response resp(200, u8R"({"status": "success", "message": "Zalogowano pomyœlnie!"})");
                resp.set_header("Set-Cookie", "X-Session-Hash=" + sessionHash + "; Path=/; HttpOnly");
                return resp;
            }
            case Database::LoginStatus::InvalidLogin: {
                // Jeœli login jest niepoprawny, zwróæ odpowiedni¹ wiadomoœæ.
                return response(200, u8R"({"status": "error", "message": "U¿ytkownik o takim loginie nie istnieje..."})");
            }
            case Database::LoginStatus::InvalidPassword: {
                // Jeœli has³o jest niepoprawne, zwróæ odpowiedni¹ wiadomoœæ.
                return response(200, u8R"({"status": "error", "message": "Niepoprawne has³o..."})");
            }
            case Database::LoginStatus::Error:
            default: {
                // W przypadku innego b³êdu, zwróæ wiadomoœæ o b³êdzie.
                return response(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas logowania..."})");
            }
            }
                });

        CROW_ROUTE(app, "/logout")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            this->sessionManager.invalidate_session(sessionHash);

            response resp(302);
            resp.set_header("Location", "/login");
            resp.set_header("Set-Cookie", "X-Session-Hash=deleted; Path=/; HttpOnly; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
            return resp;
                });

        //////////////////////////////////////////////////////////////////
        ////////////////////////// PROFIL ///////////////////////////////

        CROW_ROUTE(app, "/profile/edit")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                // Przekierowanie na stronê logowania, jeœli u¿ytkownik nie jest zalogowany
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            string login = this->sessionManager.get_user_login(sessionHash);
            Database::UserData userData = this->db.getUserData(login);
            this->sessionManager.set_user_id(sessionHash, userData.id);
            this->sessionManager.set_user_role(sessionHash, userData.role);
            this->sessionManager.set_user_firstname(sessionHash, userData.firstname);
            this->sessionManager.set_user_lastname(sessionHash, userData.lastname);
            this->sessionManager.set_user_email(sessionHash, userData.email);
            this->sessionManager.set_user_login(sessionHash, userData.login);
            this->sessionManager.set_user_password(sessionHash, userData.password);
            this->sessionManager.set_user_phonenumber(sessionHash, userData.phone_number);
            this->sessionManager.set_user_address(sessionHash, userData.address);
            this->sessionManager.set_user_city(sessionHash, userData.city);
            this->sessionManager.set_user_zip(sessionHash, userData.zip);
            this->sessionManager.set_user_session(userData.id, sessionHash);

            // U¿ytkownik jest zalogowany, kontynuacja pobierania danych u¿ytkownika
            string firstname = this->sessionManager.get_user_firstname(sessionHash);
            string lastname = this->sessionManager.get_user_lastname(sessionHash);
            string email = this->sessionManager.get_user_email(sessionHash);
            
            string password = this->sessionManager.get_user_password(sessionHash);
            string phonenumber = this->sessionManager.get_user_phonenumber(sessionHash);
            string address = this->sessionManager.get_user_address(sessionHash);
            string city = this->sessionManager.get_user_city(sessionHash);
            string zip = this->sessionManager.get_user_zip(sessionHash);
            uint32_t role = this->sessionManager.get_user_role(sessionHash);

            // Wype³nienie kontekstu szablonu danymi u¿ytkownika
            mustache::context ctx;
            ctx["logged_in"] = true;
            ctx["username"] = firstname + " " + lastname;
            ctx["firstname"] = firstname;
            ctx["lastname"] = lastname;
            ctx["email"] = email;
            ctx["login"] = login;
            ctx["password"] = password;
            ctx["phonenumber"] = phonenumber;
            ctx["address"] = address;
            ctx["city"] = city;
            ctx["zip"] = zip;
            ctx["is_admin"] = (role == 2);

            // Renderowanie strony edycji profilu z odpowiednim kontekstem
            return response(mustache::load("home/editprofile.html").render(ctx));
                });

        // Edycja u¿ytkownika
        CROW_ROUTE(app, "/profile/editUser")
            .methods(HTTPMethod::Post)
            ([this](const request& req) {
            // Przetworzenie cia³a ¿¹dania jako JSON
            auto x = json::load(req.body);
            if (!x)
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");

            // Pobranie danych z JSON
            string oldEmail = x["oldEmail"].s();
            string newEmail = x["newEmail"].s();
            string oldPassword = x["oldPassword"].s();
            string newPassword = x["newPassword"].s();
            string newFirstname = x["newFirstname"].s();
            string newLastname = x["newLastname"].s();
            string newPhoneNumber = x["newPhoneNumber"].s();
            string login = x["login"].s();
            string newAddress = x["newAddress"].s();
            string newCity = x["newCity"].s();
            string newZip = x["newZip"].s();

            if (oldEmail.empty() || newEmail.empty() || newPassword.empty() || newFirstname.empty() || newLastname.empty() || newPhoneNumber.empty() || newAddress.empty() || newCity.empty() || newZip.empty()) {
                return respond_with_json(200, u8R"({"status": "error", "message": "Brak wszystkich wymaganych danych."})");
            }

            if (oldEmail == newEmail) {
            }
            else {
                if (this->db.doesUserExist(newEmail, "email")) {
                    return response(200, u8R"({"status": "error", "message": "Ustawiony nowy email ju¿ istnieje w bazie."})");
                }
            }

            string password = (newPassword == "NULLNOTCHANGETHIS") ? oldPassword : newPassword;

            if (this->db.updateUser(oldEmail, newEmail, password, newFirstname, newLastname, newPhoneNumber, newAddress, newCity, newZip)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "U¿ytkownik zaktualizowany pomyœlnie."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas aktualizacji u¿ytkownika."})");
            }
                });


        ///////////////////////////////////////////////////////////////

        CROW_ROUTE(app, "/admin/category/")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            else if (!RoleManager::is_admin(sessionManager, sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/");
                return redirectResp;
            }

            mustache::context ctx;
            ctx["username"] = this->sessionManager.get_user_firstname(sessionHash) + " " + this->sessionManager.get_user_lastname(sessionHash);
            ctx["email"] = this->sessionManager.get_user_email(sessionHash);
            ctx["is_admin"] = (this->sessionManager.get_user_role(sessionHash) == RoleManager::ADMIN);

            return response(mustache::load("admin/category.html").render(ctx));
        });

        CROW_ROUTE(app, "/admin/category/data")
            ([this](const request& req) -> response {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");

            vector<Database::CategoryDetails> category = this->db.getAllCategory();
            json::wvalue x;
            for (size_t i = 0; i < category.size(); i++) {
                x[i]["id"] = category[i].id;
                x[i]["name"] = category[i].category_name;
            }
            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });

        CROW_ROUTE(app, "/admin/maindata")
            ([this]() {
            Database::MainData mainData = this->db.getMainData();
            json::wvalue x;

            // Konwersja danych na JSON
            x["totalUsers"] = mainData.totalUsers;
            x["activeProducts"] = mainData.activeProducts;
            x["inactiveProducts"] = mainData.inactiveProducts;
            x["totalProducts"] = mainData.totalProducts;
            x["totalAuctions"] = mainData.totalAuctions;
            x["totalSales"] = mainData.totalSales;
            x["totalImages"] = mainData.totalImages;
            x["totalImagesSize"] = mainData.totalImagesSize; // Rozmiar w bajtach

            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });


        CROW_ROUTE(app, "/admin/addCategory")
            .methods(HTTPMethod::Post)
            ([this](const request& req) {
            auto x = json::load(req.body);
            if (!x)
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");

            // Pobranie danych z JSON
            string name = x["name"].s();

            if (name.empty()) {
                return respond_with_json(200, u8R"({"status": "error", "message": "Brak wszystkich wymaganych danych."})");
            }

            if (this->db.doesCatExist(name)) {
                return response(200, u8R"({"status": "error", "message": "Kategoria o tej nazwie ju¿ istnieje..."})");
            }

            if (this->db.addCat(name)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "Kategoria dodana pomyœlnie."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas dodawania kategorii."})");
            }
                });

        CROW_ROUTE(app, "/admin/editCategory")
            .methods(HTTPMethod::Post)
            ([this](const request& req) {
            // Przetworzenie cia³a ¿¹dania jako JSON
            auto x = json::load(req.body);
            if (!x)
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");

            // Pobranie danych z JSON
            string oldName = x["oldName"].s();
            string newName = x["newName"].s();

            if (oldName.empty() || newName.empty()) {
                return respond_with_json(200, u8R"({"status": "error", "message": "Brak wszystkich wymaganych danych."})");
            }

            if (this->db.doesCatExist(newName)) {
                return response(200, u8R"({"status": "error", "message": "Ustawiona nowa nazwa kategorii ju¿ istnieje w bazie..."})");
            }

            if (this->db.updateCat(oldName, newName)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "Kategoria zaktualizowana pomyœlnie."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas aktualizacji kategorii."})");
            }
                });


        CROW_ROUTE(app, "/admin/deleteCat")
            .methods(HTTPMethod::Get)
            ([this](const request& req) {

            auto id_param = req.url_params.get("ID");

            if (!id_param) {
                return response(400, u8"Brak ID kategorii.");
            }

            int id = stoi(id_param);

            if (this->db.deleteCat(id)) {
                response resp(200, u8"Kategoria usuniêta!");
                resp.add_header("Location", "/admin/category/");
                return resp;
            }
            else {
                return response(200, u8"Wyst¹pi³ b³¹d podczas usuwania kategorii.");
            }
                });

        /////////////////////////////////////////////////////////////////////////////////////////

        CROW_ROUTE(app, "/admin/users/")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            else if (!RoleManager::is_admin(sessionManager, sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/");
                return redirectResp;
            }

            // Pobranie danych zalogowanego u¿ytkownika
            mustache::context ctx;
            ctx["username_logged"] = this->sessionManager.get_user_firstname(sessionHash) + " " + this->sessionManager.get_user_lastname(sessionHash);
            ctx["email_logged"] = this->sessionManager.get_user_email(sessionHash);
            ctx["is_admin_logged"] = (this->sessionManager.get_user_role(sessionHash) == RoleManager::ADMIN);

            return response(mustache::load("admin/users.html").render(ctx));

                });

        CROW_ROUTE(app, "/admin/products/")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            else if (!RoleManager::is_admin(sessionManager, sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/");
                return redirectResp;
            }

            // Pobranie danych zalogowanego u¿ytkownika
            mustache::context ctx;
            ctx["username_logged"] = this->sessionManager.get_user_firstname(sessionHash) + " " + this->sessionManager.get_user_lastname(sessionHash);
            ctx["email_logged"] = this->sessionManager.get_user_email(sessionHash);
            ctx["is_admin_logged"] = (this->sessionManager.get_user_role(sessionHash) == RoleManager::ADMIN);

            return response(mustache::load("admin/products.html").render(ctx));

                });

        CROW_ROUTE(app, "/admin/users/data")
            ([this](const request& req) -> response {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            else if (!RoleManager::is_admin(sessionManager, sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/");
                return redirectResp;
            }

            vector<Database::UserDetails> users = this->db.getAllUsers();
            json::wvalue x;
            for (size_t i = 0; i < users.size(); i++) {
                x[i]["id"] = users[i].id;
                x[i]["username"] = users[i].firstname + " " + users[i].lastname;
                x[i]["phone_number"] = users[i].phone_number;
                x[i]["email"] = users[i].email;
                x[i]["login"] = users[i].login;
                x[i]["password"] = users[i].password;
                x[i]["role"] = users[i].role;
                x[i]["firstname"] = users[i].firstname;
                x[i]["lastname"] = users[i].lastname;
                x[i]["address"] = users[i].address;
                x[i]["city"] = users[i].city;
                x[i]["zip"] = users[i].zip;
            }
            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });

        CROW_ROUTE(app, "/admin")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            else if (!RoleManager::is_admin(sessionManager, sessionHash)) {
                response redirectResp(302);
                redirectResp.set_header("Location", "/");
                return redirectResp;
            }

            // Pobranie danych u¿ytkownika
            mustache::context ctx;
            ctx["username"] = this->sessionManager.get_user_firstname(sessionHash) + " " + this->sessionManager.get_user_lastname(sessionHash);
            ctx["email"] = this->sessionManager.get_user_email(sessionHash);
            ctx["is_admin"] = (this->sessionManager.get_user_role(sessionHash) == RoleManager::ADMIN);

            return response(mustache::load("admin/admin.html").render(ctx));
                });

        



        // Dodawanie u¿ytkownika
        CROW_ROUTE(app, "/admin/addUser")
            .methods(HTTPMethod::Post)
            ([this](const request& req) {
            auto x = json::load(req.body);
            if (!x)
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");

            // Pobranie danych z JSON
            string firstname = x["firstname"].s();
            string lastname = x["lastname"].s();
            string email = x["email"].s();
            string login = x["login"].s();
            string password = x["password"].s();
            string phone_number = x["phone_number"].s();
            string address = x["address"].s();
            string city = x["city"].s();
            string zip = x["zip"].s();

            if (firstname.empty() || lastname.empty() || email.empty() || login.empty() || password.empty() || phone_number.empty() || address.empty() || city.empty() || zip.empty()) {
                return respond_with_json(200, u8R"({"status": "error", "message": "Brak wszystkich wymaganych danych."})");
            }

            if (this->db.doesUserExist(email, "email")) {
                return response(200, u8R"({"status": "error", "message": "U¿ytkownik o takim adresie email ju¿ istnieje!"})");
            }

            if (this->db.doesUserExist(login, "login")) {
                return response(200, u8R"({"status": "error", "message": "U¿ytkownik o takim loginie ju¿ istnieje!"})");
            }

            if (this->db.addUser(firstname, lastname, email, phone_number, login, password, address, city, zip)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "U¿ytkownik dodany pomyœlnie."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "U¿ytkownik o takim loginie ju¿ istnieje!"})");
            }
                });

        // Edycja u¿ytkownika
        CROW_ROUTE(app, "/admin/editUser")
            .methods(HTTPMethod::Post)
            ([this](const request& req) {
            // Przetworzenie cia³a ¿¹dania jako JSON
            auto x = json::load(req.body);
            if (!x)
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");

            // Pobranie danych z JSON
            string oldEmail = x["oldEmail"].s();
            string newEmail = x["newEmail"].s();
            string newPassword = x["newPassword"].s();
            string newFirstname = x["newFirstname"].s();
            string newLastname = x["newLastname"].s();
            string newPhoneNumber = x["newPhoneNumber"].s();
            string login = x["login"].s();
            string newAddress = x["address"].s();
            string newCity = x["city"].s();
            string newZip = x["zip"].s();

            if (oldEmail.empty() || newEmail.empty() || newPassword.empty() || newFirstname.empty() || newLastname.empty() || newPhoneNumber.empty() || newAddress.empty() || newCity.empty() || newZip.empty()) {
                return respond_with_json(200, u8R"({"status": "error", "message": "Brak wszystkich wymaganych danych."})");
            }

            if (oldEmail == newEmail) {
            }
            else {
                if (this->db.doesUserExist(newEmail, "email")) {
                    return response(200, u8R"({"status": "error", "message": "Ustawiony nowy email ju¿ istnieje w bazie."})");
                }
            }


            if (this->db.updateUser(oldEmail, newEmail, newPassword, newFirstname, newLastname, newPhoneNumber, newAddress, newCity, newZip)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "U¿ytkownik  zaktualizowany pomyœlnie."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas aktualizacji u¿ytkownika."})");
            }
                });



        // Usuwanie wielu u¿ytkowników
        CROW_ROUTE(app, "/admin/deleteUser")
            .methods(HTTPMethod::Post)
            ([this](const request& req) {
            auto id_param = req.body; // Oczekujemy, ¿e cia³o ¿¹dania zawiera ID rozdzielone przecinkami

            if (id_param.empty()) {
                return response(400, u8"Brak ID u¿ytkownika.");
            }

            // Usuñ "ID=" z pocz¹tku ci¹gu, jeœli istnieje
            auto pos = id_param.find("ID=");
            if (pos != string::npos) {
                id_param.erase(0, pos + 3); // Usuwa "ID="
            }

            // Zdekoduj ci¹g URL
            id_param = url_decode(id_param);

            // Rozdziel ID u¿ytkowników i stwórz wektor
            vector<int> ids;
            stringstream ss(id_param);
            string item;
            while (getline(ss, item, ',')) {
                try {
                    ids.push_back(stoi(item));
                }
                catch (const invalid_argument& e) {
                    cerr << "Invalid argument: " << item << endl;
                    return response(400, u8"Nieprawid³owy format ID.");
                }
                catch (const out_of_range& e) {
                    cerr << "Integer out of range: " << item << endl;
                    return response(400, u8"ID poza zakresem.");
                }
            }


            // Usuñ wszystkie zaznaaczone ID u¿ytkowników z bazy danych
            if (this->db.deleteUser(ids)) {
                vector<string> sessionTokens = this->sessionManager.get_user_session(ids);

                for (const auto& token : sessionTokens) {
                    this->sessionManager.invalidate_session(token);
                }
                return response(200, u8"Wszyscy zaznaczeni u¿ytkownicy zostali usuniêci. Ich produkty równie¿.");
            }
            else {
                return response(500, u8"Wyst¹pi³ b³¹d podczas usuwania u¿ytkowników.");
            }
                });

        CROW_ROUTE(app, "/admin/addProduct")
            .methods("POST"_method)([&](const request& req) {
            multipart::message msg(req);

            // Pobieranie danych produktu
            auto product_name = msg.get_part_by_name("product_name").body;
            auto product_slug = msg.get_part_by_name("product_slug").body;
            auto product_desc_long = msg.get_part_by_name("product_desc_long").body;
            auto product_desc_short = msg.get_part_by_name("product_desc_short").body;
            double product_price = stod(msg.get_part_by_name("product_price").body);
            auto product_date = msg.get_part_by_name("product_date").body;
            auto product_enddate = msg.get_part_by_name("product_enddate").body;
            auto product_delivery = msg.get_part_by_name("product_delivery").body;
            double product_delivery_cost = stod(msg.get_part_by_name("product_delivery_cost").body);
            int product_manager = stoi(msg.get_part_by_name("product_manager").body);
            int product_category = stoi(msg.get_part_by_name("product_category").body);
            int buy_now = stoi(msg.get_part_by_name("buy_now").body);
            int auction = stoi(msg.get_part_by_name("auction").body);

            if (this->db.doesProductExist(product_slug)) {
                return response(200, u8R"({"status": "error", "message": "Produkt z takim linkiem ju¿ istnieje! Ustaw nowy link!"})");
            }

            // Dodajemy produkt do bazy danych i otrzymujemy jego ID
            int product_id = db.addProduct(product_name, product_slug, product_desc_long, product_desc_short,
                product_price, product_date, product_enddate,
                product_delivery, product_delivery_cost, product_manager,
                product_category, buy_now, auction);

            if (product_id == -1) {
                return response(500, u8R"({"status": "error", "message": "Nie uda³o siê dodaæ produktu."})");
            }


            // Przetwarzanie ID obrazów
            auto image_ids_part = msg.get_part_by_name("imageIds");
            if (!image_ids_part.body.empty()) {
                istringstream ids_stream(image_ids_part.body);
                string image_id_str;
                while (getline(ids_stream, image_id_str, ',')) {
                    int image_id = stoi(image_id_str);

                    // £¹czenie produktu z obrazem
                    if (!db.linkRelations(product_id, image_id)) {
                        return response(200, u8"{\"status\": \"error\", \"message\": \"Nie uda³o siê po³¹czyæ produktu z obrazem o ID: " + image_id_str + "\"}");
                    }
                }
            }

            return response(200, u8"{\"status\": \"success\", \"message\": \"Produkt dodany pomyœlnie.\"}");
                });


        CROW_ROUTE(app, "/admin/addImage")
            .methods("POST"_method)([&](const request& req) {
            multipart::message msg(req);
            auto part = msg.get_part_by_name("image");
            auto disposition = multipart::get_header_object(part.headers, "content-disposition");
            string image_name = disposition.params["filename"];
            string image_path = "static/uploads/" + image_name;

            ofstream out(image_path, ios::binary);
            if (!out) {
                return response(500, "{\"status\": \"error\", \"message\": \"Nie uda³o siê zapisaæ pliku.\"}");
            }
            out.write(part.body.data(), part.body.size());
            out.close();

            int image_id = db.addImage(image_name, image_path);
            if (image_id == -1) {
                return response(500, "{\"status\": \"error\", \"message\": \"Nie uda³o siê dodaæ obrazu do bazy danych.\"}");
            }
            return response(200, "{\"status\": \"success\", \"imageId\": " + to_string(image_id) + "}");
                });

        CROW_ROUTE(app, "/product/edit").methods("POST"_method)([&](const request& req) {
            auto x = json::load(req.body);
            if (!x) {
                return response(400, u8R"({"status": "error", "message": "Nieprawid³owe dane JSON."})");
            }

            // Pobieranie danych z JSON
            string product_name = x["product_name"].s();
            string product_slug = x["product_slug"].s();
            string product_desc_long = x["product_desc_long"].s();
            string product_desc_short = x["product_desc_short"].s();
            double product_price = x["product_price"].d();
            string product_date = x["product_date"].s();
            string product_enddate = x["product_enddate"].s();
            string product_delivery = x["product_delivery"].s();
            double product_delivery_cost = x["product_delivery_cost"].d();
            int product_manager = x["product_manager"].i();
            int product_category = x["product_category"].i();
            int buy_now = x["buy_now"].i();
            int auction = x["auction"].i();
            int archived = 0;

            // Sprawdzenie, czy wymagane pola s¹ dostêpne
            if (product_name.empty() || product_slug.empty()) {
                return response(400, u8R"({"status": "error", "message": "Brak wszystkich wymaganych danych."})");
            }

            // Aktualizacja produktu w bazie danych
            if (!db.updateProduct(product_slug, product_name, product_desc_long, product_desc_short,
                product_price, product_date, product_enddate,
                product_delivery, product_delivery_cost, product_manager,
                product_category, buy_now, auction, archived)) {
                return response(500, u8R"({"status": "error", "message": "Nie uda³o siê zaktualizowaæ produktu."})");
            }

            return response(200, u8R"({"status": "success", "message": "Produkt zaktualizowany pomyœlnie."})");
            });





        CROW_ROUTE(app, "/product/add")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                // Przekierowanie na stronê logowania, jeœli u¿ytkownik nie jest zalogowany
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }


            // U¿ytkownik jest zalogowany, kontynuacja pobierania danych u¿ytkownika
            string firstname = this->sessionManager.get_user_firstname(sessionHash);
            string lastname = this->sessionManager.get_user_lastname(sessionHash);
            string email = this->sessionManager.get_user_email(sessionHash);
            string login = this->sessionManager.get_user_login(sessionHash);
            string password = this->sessionManager.get_user_password(sessionHash);
            string phonenumber = this->sessionManager.get_user_phonenumber(sessionHash);
            uint32_t role = this->sessionManager.get_user_role(sessionHash);
            uint32_t id = this->sessionManager.get_user_id(sessionHash);

            // Wype³nienie kontekstu szablonu danymi u¿ytkownika
            mustache::context ctx;
            ctx["logged_in"] = true;
            ctx["username"] = firstname + " " + lastname;
            ctx["firstname"] = firstname;
            ctx["lastname"] = lastname;
            ctx["email"] = email;
            ctx["login"] = login;
            ctx["password"] = password;
            ctx["phonenumber"] = phonenumber;
            ctx["is_admin"] = (role == 2);
            ctx["user_id"] = id;

            return response(mustache::load("home/product_add.html").render(ctx));
                });

        CROW_ROUTE(app, "/products/data")
            ([this]() -> response {
            vector<Database::ProductDetails> products = this->db.getAllProducts();
            json::wvalue x;
            for (size_t i = 0; i < products.size(); i++) {
                x[i]["id"] = products[i].id;
                x[i]["name"] = products[i].name;
                x[i]["slug"] = products[i].slug;
                x[i]["descLong"] = products[i].descLong;
                x[i]["descShort"] = products[i].descShort;
                x[i]["price"] = products[i].price;
                x[i]["date"] = products[i].date;
                x[i]["endDate"] = products[i].endDate;
                x[i]["delivery"] = products[i].delivery;
                x[i]["deliveryCost"] = products[i].deliveryCost;
                x[i]["manager"] = products[i].manager;
                x[i]["category"] = products[i].categoryName;
                x[i]["buyNow"] = products[i].buyNow;
                x[i]["auction"] = products[i].auction;
                x[i]["archived"] = products[i].archived;
                for (size_t j = 0; j < products[i].images.size(); ++j) {
                    x[i]["images"][j]["name"] = products[i].images[j].first;
                    x[i]["images"][j]["path"] = products[i].images[j].second;
                }
                
            }
            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });

        CROW_ROUTE(app, "/admin/products/data")
            ([this]() -> response {
            vector<Database::ProductDetails> products = this->db.getAllProducts();
            json::wvalue x;

            for (size_t i = 0; i < products.size(); i++) {
                // Pobierz szczegó³y mened¿era dla ka¿dego produktu
                Database::UserDetails managerDetails = this->db.getUserDetailsById(products[i].manager);

                x[i]["id"] = products[i].id;
                x[i]["name"] = products[i].name;
                x[i]["slug"] = products[i].slug;
                x[i]["descLong"] = products[i].descLong;
                x[i]["descShort"] = products[i].descShort;
                x[i]["price"] = products[i].price;
                x[i]["date"] = products[i].date;
                x[i]["endDate"] = products[i].endDate;
                x[i]["delivery"] = products[i].delivery;
                x[i]["deliveryCost"] = products[i].deliveryCost;
                x[i]["manager_name"] = managerDetails.firstname + " " + managerDetails.lastname; // Imiê i nazwisko sprzedawcy
                x[i]["manager"] = products[i].manager;
                x[i]["category"] = products[i].categoryName;
                x[i]["buyNow"] = products[i].buyNow;
                x[i]["auction"] = products[i].auction;

                for (size_t j = 0; j < products[i].images.size(); ++j) {
                    x[i]["images"][j]["name"] = products[i].images[j].first;
                    x[i]["images"][j]["path"] = products[i].images[j].second;
                }
            }

            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });


        
       


        CROW_ROUTE(app, "/products/user/data")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                // Przekierowanie na stronê logowania, jeœli u¿ytkownik nie jest zalogowany
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            uint32_t user_id = this->sessionManager.get_user_id(sessionHash);

            vector<Database::UserProductDetails> products = this->db.getUserProducts(user_id);
            json::wvalue x;
            for (size_t i = 0; i < products.size(); i++) {
                x[i]["id"] = products[i].id;
                x[i]["name"] = products[i].name;
                x[i]["slug"] = products[i].slug;
                x[i]["descLong"] = products[i].descLong;
                x[i]["descShort"] = products[i].descShort;
                x[i]["price"] = products[i].price;
                x[i]["date"] = products[i].date;
                x[i]["endDate"] = products[i].endDate;
                x[i]["delivery"] = products[i].delivery;
                x[i]["deliveryCost"] = products[i].deliveryCost;
                x[i]["manager"] = products[i].manager;
                x[i]["category"] = products[i].categoryName;
                x[i]["buyNow"] = products[i].buyNow;
                x[i]["auction"] = products[i].auction;
                x[i]["archived"] = products[i].archived;
                for (size_t j = 0; j < products[i].images.size(); ++j) {
                    x[i]["images"][j]["name"] = products[i].images[j].first;
                    x[i]["images"][j]["path"] = products[i].images[j].second;
                }
            }
            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });

        CROW_ROUTE(app, "/products/user")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                // Przekierowanie na stronê logowania, jeœli u¿ytkownik nie jest zalogowany
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }


            // U¿ytkownik jest zalogowany, kontynuacja pobierania danych u¿ytkownika
            string firstname = this->sessionManager.get_user_firstname(sessionHash);
            string lastname = this->sessionManager.get_user_lastname(sessionHash);
            string email = this->sessionManager.get_user_email(sessionHash);
            string login = this->sessionManager.get_user_login(sessionHash);
            string password = this->sessionManager.get_user_password(sessionHash);
            string phonenumber = this->sessionManager.get_user_phonenumber(sessionHash);
            uint32_t role = this->sessionManager.get_user_role(sessionHash);
            uint32_t id = this->sessionManager.get_user_id(sessionHash);

            // Wype³nienie kontekstu szablonu danymi u¿ytkownika
            mustache::context ctx;
            ctx["logged_in"] = true;
            ctx["username"] = firstname + " " + lastname;
            ctx["firstname"] = firstname;
            ctx["lastname"] = lastname;
            ctx["email"] = email;
            ctx["login"] = login;
            ctx["password"] = password;
            ctx["phonenumber"] = phonenumber;
            ctx["is_admin"] = (role == 2);
            ctx["user_id"] = id;

            return response(mustache::load("home/product_user.html").render(ctx));
                });

        CROW_ROUTE(app, "/shopping/user/data")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                // Przekierowanie na stronê logowania, jeœli u¿ytkownik nie jest zalogowany
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            uint32_t user_id = this->sessionManager.get_user_id(sessionHash);

            vector<Database::ShoppingDetails> shoppingDetails = this->db.getUserShopping(user_id);
            json::wvalue x;
            for (size_t i = 0; i < shoppingDetails.size(); i++) {
                x[i]["saleId"] = shoppingDetails[i].saleId;
                x[i]["sellerFirstName"] = shoppingDetails[i].sellerFirstName;
                x[i]["sellerLastName"] = shoppingDetails[i].sellerLastName;
                x[i]["sellerPhoneNumber"] = shoppingDetails[i].sellerPhoneNumber;
                x[i]["productName"] = shoppingDetails[i].productName;
                x[i]["productSlug"] = shoppingDetails[i].productSlug;
                x[i]["productPrice"] = shoppingDetails[i].productPrice;
                x[i]["productDelivery"] = shoppingDetails[i].productDelivery;
                x[i]["productDeliveryCost"] = shoppingDetails[i].productDeliveryCost;
                x[i]["buyerFirstName"] = shoppingDetails[i].buyerFirstName;
                x[i]["buyerLastName"] = shoppingDetails[i].buyerLastName;
                x[i]["buyerPhoneNumber"] = shoppingDetails[i].buyerPhoneNumber;
                x[i]["buyerAddress"] = shoppingDetails[i].buyerAddress;
                x[i]["buyerCity"] = shoppingDetails[i].buyerCity;
                x[i]["buyerZip"] = shoppingDetails[i].buyerZip;
                x[i]["saleDate"] = shoppingDetails[i].saleDate;
                x[i]["status"] = shoppingDetails[i].status;
            }
            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });

        CROW_ROUTE(app, "/sales/user/data")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                // Przekierowanie na stronê logowania, jeœli u¿ytkownik nie jest zalogowany
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }
            uint32_t user_id = this->sessionManager.get_user_id(sessionHash);

            vector<Database::SalesDetails> shoppingDetails = this->db.getUserSales(user_id);
            json::wvalue x;
            for (size_t i = 0; i < shoppingDetails.size(); i++) {
                x[i]["saleId"] = shoppingDetails[i].saleId;
                x[i]["sellerFirstName"] = shoppingDetails[i].sellerFirstName;
                x[i]["sellerLastName"] = shoppingDetails[i].sellerLastName;
                x[i]["sellerPhoneNumber"] = shoppingDetails[i].sellerPhoneNumber;
                x[i]["productName"] = shoppingDetails[i].productName;
                x[i]["productSlug"] = shoppingDetails[i].productSlug;
                x[i]["productPrice"] = shoppingDetails[i].productPrice;
                x[i]["productDelivery"] = shoppingDetails[i].productDelivery;
                x[i]["productDeliveryCost"] = shoppingDetails[i].productDeliveryCost;
                x[i]["buyerFirstName"] = shoppingDetails[i].buyerFirstName;
                x[i]["buyerLastName"] = shoppingDetails[i].buyerLastName;
                x[i]["buyerPhoneNumber"] = shoppingDetails[i].buyerPhoneNumber;
                x[i]["buyerAddress"] = shoppingDetails[i].buyerAddress;
                x[i]["buyerCity"] = shoppingDetails[i].buyerCity;
                x[i]["buyerZip"] = shoppingDetails[i].buyerZip;
                x[i]["saleDate"] = shoppingDetails[i].saleDate;
                x[i]["status"] = shoppingDetails[i].status;
            }
            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });


        CROW_ROUTE(app, "/shopping/user")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                // Przekierowanie na stronê logowania, jeœli u¿ytkownik nie jest zalogowany
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }


            // U¿ytkownik jest zalogowany, kontynuacja pobierania danych u¿ytkownika
            string firstname = this->sessionManager.get_user_firstname(sessionHash);
            string lastname = this->sessionManager.get_user_lastname(sessionHash);
            string email = this->sessionManager.get_user_email(sessionHash);
            string login = this->sessionManager.get_user_login(sessionHash);
            string password = this->sessionManager.get_user_password(sessionHash);
            string phonenumber = this->sessionManager.get_user_phonenumber(sessionHash);
            uint32_t role = this->sessionManager.get_user_role(sessionHash);
            uint32_t id = this->sessionManager.get_user_id(sessionHash);

            // Wype³nienie kontekstu szablonu danymi u¿ytkownika
            mustache::context ctx;
            ctx["logged_in"] = true;
            ctx["username"] = firstname + " " + lastname;
            ctx["firstname"] = firstname;
            ctx["lastname"] = lastname;
            ctx["email"] = email;
            ctx["login"] = login;
            ctx["password"] = password;
            ctx["phonenumber"] = phonenumber;
            ctx["is_admin"] = (role == 2);
            ctx["user_id"] = id;

            return response(mustache::load("home/shopping.html").render(ctx));
                });

        CROW_ROUTE(app, "/sales/user")
            ([this](const request& req) {
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            if (sessionHash.empty() || !this->sessionManager.validate_session(sessionHash)) {
                // Przekierowanie na stronê logowania, jeœli u¿ytkownik nie jest zalogowany
                response redirectResp(302);
                redirectResp.set_header("Location", "/login");
                return redirectResp;
            }


            // U¿ytkownik jest zalogowany, kontynuacja pobierania danych u¿ytkownika
            string firstname = this->sessionManager.get_user_firstname(sessionHash);
            string lastname = this->sessionManager.get_user_lastname(sessionHash);
            string email = this->sessionManager.get_user_email(sessionHash);
            string login = this->sessionManager.get_user_login(sessionHash);
            string password = this->sessionManager.get_user_password(sessionHash);
            string phonenumber = this->sessionManager.get_user_phonenumber(sessionHash);
            uint32_t role = this->sessionManager.get_user_role(sessionHash);
            uint32_t id = this->sessionManager.get_user_id(sessionHash);

            // Wype³nienie kontekstu szablonu danymi u¿ytkownika
            mustache::context ctx;
            ctx["logged_in"] = true;
            ctx["username"] = firstname + " " + lastname;
            ctx["firstname"] = firstname;
            ctx["lastname"] = lastname;
            ctx["email"] = email;
            ctx["login"] = login;
            ctx["password"] = password;
            ctx["phonenumber"] = phonenumber;
            ctx["is_admin"] = (role == 2);
            ctx["user_id"] = id;

            return response(mustache::load("home/sales.html").render(ctx));
                });

        CROW_ROUTE(app, "/products/<string>/data")
            ([this](const string& slug) {
            Database::ProductDetailsWithImages product = this->db.getProductBySlug(slug);
            json::wvalue x;

            if (product.id != -1) {
                // Pobranie danych managera (sprzedawcy)
                Database::UserDetails managerDetails = this->db.getUserDetailsById(product.manager);
                Database::UserDetails auctionDetails = this->db.getUserDetailsById(product.currentBidder);

                x["id"] = product.id;
                x["name"] = product.name;
                x["slug"] = product.slug;
                x["descLong"] = product.descLong;
                x["descShort"] = product.descShort;
                x["price"] = product.price;
                x["date"] = product.date;
                x["endDate"] = product.endDate;
                x["delivery"] = product.delivery;
                x["deliveryCost"] = product.deliveryCost;
                x["manager"] = managerDetails.firstname + " " + managerDetails.lastname; // Imiê i nazwisko sprzedawcy
                x["manager_id"] = product.manager;
                x["manager_phone"] = managerDetails.phone_number; // Numer telefonu sprzedawcy
                x["category"] = product.categoryName;
                x["buyNow"] = product.buyNow;
                x["auction"] = product.auction;
                x["archived"] = product.archived;
                x["currentbidder"] = auctionDetails.email;

                // Dodawanie obrazów produktu
                for (size_t j = 0; j < product.images.size(); ++j) {
                    x["images"][j]["name"] = product.images[j].first;
                    x["images"][j]["path"] = product.images[j].second;
                }
            }
            else {
                x["error"] = "Product not found";
            }

            response jsonResp = response(x);
            jsonResp.add_header("Content-Type", "application/json");
            return jsonResp;
                });


        CROW_ROUTE(app, "/product/delete").methods("POST"_method)
            ([this](const request& req) {
            // 1. Odebranie danych z ¿¹dania
            auto jsonValue = json::load(req.body);
            if (!jsonValue || !jsonValue.has("slug")) {
                return response(400, "Nieprawid³owe dane");
            }
            string productSlug = jsonValue["slug"].s();

            // 2. Wyszukanie produktu w bazie danych
            auto productDetails = db.getProductBySlug(productSlug);
            if (productDetails.id == -1) {
                return response(404, "Produkt nie znaleziony");
            }

            // 3. Pobranie identyfikatorów obrazów
            vector<int> imageIds = db.getImageIds(productDetails.id);

            // 4. Usuwanie plików obrazów i ich identyfikatorów
            vector<string> imagePaths = db.getImagesPaths(productDetails.id);
            for (const auto& path : imagePaths) {
                remove(path.c_str()); // Usuwanie pliku
            }

            // 5.Usuwanie powi¹zañ z tabeli ProductImages
            db.deleteProductImages(productDetails.id);

            // 6.Usuwanie obrazów z bazy danych
            db.deleteImages(imageIds);

            // 7. Usuwanie produktu z tabeli Products
            db.deleteProduct(productDetails.id);

            // 8. OdpowiedŸ do klienta
            return response(200, "Produkt usuniêty");
                });

        CROW_ROUTE(app, "/product/imgdelete").methods("POST"_method)
            ([this](const request& req) {
            // 1. Odebranie danych z ¿¹dania
            auto jsonValue = json::load(req.body);
            if (!jsonValue || !jsonValue.has("productId") || !jsonValue.has("imageName")) {
                return response(400, "Nieprawid³owe dane");
            }
            int productId = jsonValue["productId"].i();
            string imageName = jsonValue["imageName"].s();

            // 2. Znalezienie ID obrazu w bazie danych
            int imageId = db.getImageIdByName(imageName);
            if (imageId == -1) {
                return response(404, "Obraz nie znaleziony");
            }

            // 3. Usuwanie powi¹zania obrazu z produktem
            if (!db.deleteProductImage(productId, imageId)) {
                return response(500, "B³¹d podczas usuwania powi¹zania obrazu z produktem");
            }

            // 4. Usuwanie fizycznego pliku obrazu
            string imagePath = db.getImagePath(imageId);
            remove(imagePath.c_str());

            // 5. Usuwanie obrazu z bazy danych
            if (!db.deleteImage(imageId)) {
                return response(500, "B³¹d podczas usuwania obrazu z bazy danych");
            }

            // 6. OdpowiedŸ do klienta
            return response(200, "Obraz usuniêty");
                });




        CROW_ROUTE(app, "/products/<string>")
            ([this](const request& req, const string& slug) {
            mustache::context ctx;
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            bool is_logged_in = !sessionHash.empty() && this->sessionManager.validate_session(sessionHash);
            auto user_id = this->sessionManager.get_user_id(sessionHash);
            if (user_id == 0 || !this->db.isUserExist(user_id)) {
                is_logged_in = false;
            }
            ctx["logged_in"] = is_logged_in; // Dodanie informacji o logowaniu do kontekstu

            if (is_logged_in) {
                // Pobranie danych u¿ytkownika, gdy jest zalogowany
                uint32_t id = this->sessionManager.get_user_id(sessionHash);
                string firstname = this->sessionManager.get_user_firstname(sessionHash);
                string lastname = this->sessionManager.get_user_lastname(sessionHash);
                string email = this->sessionManager.get_user_email(sessionHash);
                uint32_t role = this->sessionManager.get_user_role(sessionHash);

                // Wype³nienie kontekstu szablonu danymi u¿ytkownika
                ctx["id"] = id;
                ctx["username"] = firstname + " " + lastname;
                ctx["email"] = email;
                ctx["is_admin"] = (role == 2);
            }


            // Renderowanie strony produktu z odpowiednim kontekstem
            return response(mustache::load("home/product.html").render(ctx));
                });

        CROW_ROUTE(app, "/category/<string>")
            ([this](const request& req, const string& slug) {
            mustache::context ctx;
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            bool is_logged_in = !sessionHash.empty() && this->sessionManager.validate_session(sessionHash);
            auto user_id = this->sessionManager.get_user_id(sessionHash);
            if (user_id == 0 || !this->db.isUserExist(user_id)) {
                is_logged_in = false;
            }
            ctx["logged_in"] = is_logged_in; // Dodanie informacji o logowaniu do kontekstu

            if (is_logged_in) {
                // Pobranie danych u¿ytkownika, gdy jest zalogowany
                uint32_t id = this->sessionManager.get_user_id(sessionHash);
                string firstname = this->sessionManager.get_user_firstname(sessionHash);
                string lastname = this->sessionManager.get_user_lastname(sessionHash);
                string email = this->sessionManager.get_user_email(sessionHash);
                uint32_t role = this->sessionManager.get_user_role(sessionHash);

                // Wype³nienie kontekstu szablonu danymi u¿ytkownika
                ctx["id"] = id;
                ctx["username"] = firstname + " " + lastname;
                ctx["email"] = email;
                ctx["is_admin"] = (role == 2);
            }


            // Renderowanie strony produktu z odpowiednim kontekstem
            return response(mustache::load("home/category.html").render(ctx));
                });

        CROW_ROUTE(app, "/products")
            ([this](const request& req) {
            mustache::context ctx;
            string sessionHash = this->get_cookie_value(req, "X-Session-Hash");
            cout << "Otrzymane ciasteczko: " << sessionHash << endl;

            // Sprawdzenie, czy u¿ytkownik jest zalogowany
            bool is_logged_in = !sessionHash.empty() && this->sessionManager.validate_session(sessionHash);
            auto user_id = this->sessionManager.get_user_id(sessionHash);
            if (user_id == 0 || !this->db.isUserExist(user_id)) {
                is_logged_in = false;
            }
            ctx["logged_in"] = is_logged_in; // Dodanie informacji o logowaniu do kontekstu

            if (is_logged_in) {
                // Pobranie danych u¿ytkownika, gdy jest zalogowany
                string firstname = this->sessionManager.get_user_firstname(sessionHash);
                string lastname = this->sessionManager.get_user_lastname(sessionHash);
                string email = this->sessionManager.get_user_email(sessionHash);
                uint32_t role = this->sessionManager.get_user_role(sessionHash);

                // Wype³nienie kontekstu szablonu danymi u¿ytkownika
                ctx["username"] = firstname + " " + lastname;
                ctx["email"] = email;
                ctx["is_admin"] = (role == 2);
            }
            else {
                response resp(mustache::load("home/products.html").render(ctx));
                resp.set_header("Set-Cookie", "X-Session-Hash=deleted; Path=/; HttpOnly; Expires=Thu, 01 Jan 1970 00:00:00 GMT");
                return resp;
            }

            // Renderowanie strony g³ównej z odpowiednim kontekstem
            return response(mustache::load("home/products.html").render(ctx));
                });

        CROW_ROUTE(app, "/buy").methods(HTTPMethod::Post)([this](const request& req) {
            auto x = json::load(req.body);
            if (!x) {
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");
            }

            int buyerID = x["buyerID"].i();
            int sellerID = x["sellerID"].i();
            int productID = x["productID"].i();
            string saleDate = x["purchaseDate"].s();
            string status = x["status"].s();

            if (this->db.addSale(buyerID, sellerID, productID, saleDate, status)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "Produkt zosta³ zakupiony pomyœlnie."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas dodawania informacji o sprzeda¿y."})");
            }
            });

        CROW_ROUTE(app, "/buy/status").methods(HTTPMethod::Post)([this](const request& req) {
            auto x = json::load(req.body);
            if (!x) {
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");
            }

            int buyerID = x["saleId"].i();
            string status = x["newStatus"].s();

            if (this->db.changeStatus(buyerID, status)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "Status Zaktualizowany."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas dodawania informacji o sprzeda¿y."})");
            }
            });

        CROW_ROUTE(app, "/bid/end").methods(HTTPMethod::Post)([this](const request& req) {
            auto x = json::load(req.body);
            if (!x) {
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");
            }

            int auction = x["productId"].i();

            if (this->db.endAuction(auction)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "Aukcja zakoñczona!."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas dodawania informacji o sprzeda¿y."})");
            }
            });

        CROW_ROUTE(app, "/bid").methods(HTTPMethod::Post)([this](const request& req) {
            auto x = json::load(req.body);
            if (!x) {
                return respond_with_json(200, u8"{\"error\": \"Nieprawid³owe dane JSON.\"}");
            }

            int buyerID = x["buyerID"].i();
            int sellerID = x["sellerID"].i();
            int productID = x["productID"].i();
            string saleDate = x["purchaseDate"].s();
            string status = x["status"].s();
            string bidStr = x["newPrice"].s();
            double bid = stod(bidStr);

            if (this->db.addAuction(buyerID, sellerID, productID, saleDate, status, bid)) {
                return respond_with_json(200, u8R"({"status": "success", "message": "Produkt zosta³ podbity pomyœlnie."})");
            }
            else {
                return respond_with_json(200, u8R"({"status": "error", "message": "Wyst¹pi³ b³¹d podczas dodawania informacji o sprzeda¿y."})");
            }
            });


    }

    void run() {
        app.port(5665).multithreaded().run();
    }
};

int main() {
    WebApp webAppInstance;
    webAppInstance.run();
}