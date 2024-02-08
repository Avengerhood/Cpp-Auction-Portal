#include <string>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <vector>
#include <stdlib.h>
#include <utility>
#include <memory>
#include <fstream>
#include <filesystem>
#include <cstdio>
#include <algorithm>
#include <cctype>

//BAZA DANYCH MYSQL CONNECTOR WERSJA DEBUG
#include "mysql_driver.h"
#include "mysql_connection.h"
#include "mysql_error.h"
#include "cppconn/driver.h"
#include "cppconn/exception.h"
#include "cppconn/resultset.h"
#include "cppconn/statement.h"
#include "cppconn/prepared_statement.h"
#include "cppconn/metadata.h"
#include "cppconn/resultset_metadata.h"
#include "cppconn/warning.h"

using namespace std;


class Database {
private:
    sql::mysql::MySQL_Driver* driver;
    sql::Connection* conn;
    string dbname;
    string host_db;
    string username_db;
    string password_db;

    void reconnect() {
        if (conn == nullptr || conn->isClosed()) {
            try {
                driver = sql::mysql::get_mysql_driver_instance();
                conn = driver->connect(host_db, username_db, password_db);
                conn->setSchema(dbname);
            }
            catch (sql::SQLException& e) {
                cerr << "SQLException in reconnect(): " << e.what() << endl;
            }
        }
    }


public:
    Database(const string& dbname, const string& host = "tcp://127.0.0.1:3306",
        const string& username = "root", const string& password = "")
        : dbname(dbname), host_db(host), username_db(username), password_db(password) {
        reconnect();

   

        const char* create_table_sql = R"(
            CREATE TABLE IF NOT EXISTS users (
                id INT AUTO_INCREMENT PRIMARY KEY,
                firstname VARCHAR(255) NOT NULL,
                lastname VARCHAR(255) NOT NULL,
                email VARCHAR(255) NOT NULL UNIQUE,
                phone_number VARCHAR(255) NOT NULL,
                login VARCHAR(255) NOT NULL UNIQUE,
                password VARCHAR(255) NOT NULL,
                address VARCHAR(255) NOT NULL,
                city VARCHAR(255) NOT NULL,
                zip VARCHAR(255) NOT NULL,
                role INT DEFAULT 0
            );

            

            CREATE TABLE IF NOT EXISTS category (
                id INT AUTO_INCREMENT PRIMARY KEY,
                category_name VARCHAR(255) NOT NULL
            );

            CREATE TABLE IF NOT EXISTS Images (
                id INT AUTO_INCREMENT PRIMARY KEY,
                name VARCHAR(255) NOT NULL,
                path VARCHAR(255) NOT NULL
            ); 
   
            CREATE TABLE IF NOT EXISTS Products (
                id INT AUTO_INCREMENT PRIMARY KEY,
                product_name VARCHAR(255) NOT NULL,
                product_slug VARCHAR(255) NOT NULL,
                product_desc_long TEXT NOT NULL,
                product_desc_short TEXT NOT NULL,
                product_price DECIMAL(10, 2) NOT NULL,
                product_date DATETIME NOT NULL,
                product_enddate DATETIME NOT NULL,
                product_delivery VARCHAR(255) NOT NULL,
                product_delivery_cost DECIMAL(10, 2) NOT NULL,
                product_manager INT NOT NULL,
                product_category INT NOT NULL,
                buy_now BOOLEAN NOT NULL DEFAULT 0,
                auction BOOLEAN NOT NULL DEFAULT 0,
                archived BOOLEAN NOT NULL DEFAULT 0,
                FOREIGN KEY (product_manager) REFERENCES users(id),
                FOREIGN KEY (product_category) REFERENCES category(id)
            );

            CREATE TABLE IF NOT EXISTS sale (
                id INT AUTO_INCREMENT PRIMARY KEY,
                buyerID INT NOT NULL,
                sellerID INT NOT NULL,
                productID INT NOT NULL UNIQUE,
                saleDate DATETIME NOT NULL,
                status VARCHAR(255) NOT NULL UNIQUE,
                FOREIGN KEY (buyerID) REFERENCES users(id),
                FOREIGN KEY (sellerID) REFERENCES users(id),
                FOREIGN KEY (productID) REFERENCES Products(id)
            );

            CREATE TABLE IF NOT EXISTS auction (
                id INT AUTO_INCREMENT PRIMARY KEY,
                buyerID INT NOT NULL,
                sellerID INT NOT NULL,
                productID INT NOT NULL UNIQUE,
                saleDate DATETIME NOT NULL,
                status VARCHAR(255) NOT NULL UNIQUE,
                FOREIGN KEY (buyerID) REFERENCES users(id),
                FOREIGN KEY (sellerID) REFERENCES users(id),
                FOREIGN KEY (productID) REFERENCES Products(id)
            );

            CREATE TABLE IF NOT EXISTS ProductImages (
                product_id INT NOT NULL,
                image_id INT NOT NULL,
                FOREIGN KEY (product_id) REFERENCES Products(id),
                FOREIGN KEY (image_id) REFERENCES Images(id),
                PRIMARY KEY (product_id, image_id)
            )
        )";

        try {
            unique_ptr<sql::Statement> stmt(conn->createStatement());

            // Wykonaj zapytania, rozdzielaj¹c je na poszczególne instrukcje
            istringstream sqlStream(create_table_sql);
            string line;
            while (getline(sqlStream, line)) {
                if (line.find("CREATE TABLE") != string::npos) {
                    string createTableSql;
                    do {
                        createTableSql += line + " ";
                        getline(sqlStream, line);
                    } while (line.find(";") == string::npos && !sqlStream.eof());
                    createTableSql += line;
                    stmt->execute(createTableSql);
                }
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException: " << e.what() << endl;
        }


        // Dodawanie konta administratora jeœli nie istnieje
        if (!doesUserExist("admin", "login")) {
            addUser("Admin", "Admin", "admin@email.com", "123456789", "admin", "admin", "localhost", "server", "127-000", 2);
        }

        // Archiwizacja przeterminowanych produktów
        archiveExpiredProductsOnStart();
    }

    string xorEncryptDecrypt(const string& input, const char key) {
        string output = input;

        for (size_t i = 0; i < input.size(); i++) {
            output[i] = input[i] ^ key;
        }

        return output;
    }



    void archiveExpiredProductsOnStart() {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string today = getCurrentDate(); // Funkcja zwracaj¹ca dzisiejsz¹ datê w formacie "YYYY-MM-DD"

        try {
            // Znajdowanie produktów, których data zakoñczenia minê³a
            string findExpiredProductsSql = "SELECT id FROM Products WHERE product_enddate < ?";
            unique_ptr<sql::PreparedStatement> pstmtFind(conn->prepareStatement(findExpiredProductsSql));
            pstmtFind->setString(1, today);
            sql::ResultSet* resExpired = pstmtFind->executeQuery();

            while (resExpired->next()) {
                int productID = resExpired->getInt("id");

                // Sprawdzanie, czy produkt znajduje siê w tabeli auction
                string checkAuctionSql = "SELECT COUNT(*) AS count FROM auction WHERE productID = ?";
                unique_ptr<sql::PreparedStatement> pstmtCheck(conn->prepareStatement(checkAuctionSql));
                pstmtCheck->setInt(1, productID);
                sql::ResultSet* resCheck = pstmtCheck->executeQuery();

                if (resCheck->next() && resCheck->getInt("count") > 0) {
                    // Jeœli produkt jest w tabeli auction, zakoñcz aukcjê
                    endAuction(productID);
                }
                else {
                    // Aktualizacja produktu w tabeli Products jako zarchiwizowany
                    string updateProductSql = "UPDATE Products SET archived = 1 WHERE id = ?";
                    unique_ptr<sql::PreparedStatement> pstmtUpdateProduct(conn->prepareStatement(updateProductSql));
                    pstmtUpdateProduct->setInt(1, productID);
                    pstmtUpdateProduct->executeUpdate();
                }

                delete resCheck; // Pamiêtaj, aby zwolniæ surowy wskaŸnik
            }

            delete resExpired; // Pamiêtaj, aby zwolniæ surowy wskaŸnik
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in archiveExpiredProductsOnStart(): " << e.what() << endl;
        }
    }


    string getCurrentDate() {
        time_t now = time(nullptr);
        tm ltm;
        localtime_s(&ltm, &now);

        stringstream ss;
        ss << 1900 + ltm.tm_year << "-"
            << setw(2) << setfill('0') << 1 + ltm.tm_mon << "-"
            << setw(2) << setfill('0') << ltm.tm_mday;

        return ss.str();
    }


    bool addUser(const string& firstname, const string& lastname, const string& email,
        const string& phone_number, const string& login, string password,
        const string& address, const string& city, const string& zip, int role = 0) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        password = xorEncryptDecrypt(password, 'snpp');
        string sql = "INSERT INTO users (firstname, lastname, email, phone_number, login, password, address, city, zip, role) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

        
        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, firstname);
            pstmt->setString(2, lastname);
            pstmt->setString(3, email);
            pstmt->setString(4, phone_number);
            pstmt->setString(5, login);
            pstmt->setString(6, password);
            pstmt->setString(7, address);
            pstmt->setString(8, city);
            pstmt->setString(9, zip);
            pstmt->setInt(10, role);

            // Wykonanie zapytania
            reconnect();
            int result = pstmt->executeUpdate();
            return result > 0;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in addUser(): " << e.what() << endl;
        }
        return false;
    }


    enum class LoginStatus {
        Success,
        InvalidLogin,
        InvalidPassword,
        Error
    };

    LoginStatus checkCredentials(const string& login, const string& password) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        // Najpierw sprawdzamy, czy istnieje u¿ytkownik o danym loginie
        string userSql = "SELECT password FROM users WHERE login=?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(userSql));
            pstmt->setString(1, login);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Jeœli u¿ytkownik nie istnieje
            if (!res->next()) {
                return LoginStatus::InvalidLogin;
            }

            // Pobieranie zahashowanego has³a z bazy danych
            string storedPassword = res->getString("password");

            // Deszyfrowanie przechowywanego has³a
            string decryptedStoredPassword = xorEncryptDecrypt(storedPassword, 'snpp');

            // Porównanie z deszyfrowanym has³em
            if (decryptedStoredPassword != password) {
                return LoginStatus::InvalidPassword;
            }

            // Jeœli wszystko jest ok
            return LoginStatus::Success;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in checkCredentials(): " << e.what() << endl;
            return LoginStatus::Error;
        }
    }


    bool doesUserExist(const string& value, const string& column) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "SELECT * FROM users WHERE " + column + "=?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, value);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Sprawdzenie, czy wynik zawiera jakiekolwiek wiersze
            return res->next();
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in doesUserExist(): " << e.what() << endl;
        }
        return false;
    }



    bool isUserExist(int id) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "SELECT * FROM users WHERE id = ?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, id);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Sprawdzenie, czy wynik zawiera jakiekolwiek wiersze
            return res->next();
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in isUserExist(): " << e.what() << endl;
            return false;
        }
    }

    bool updateUser(const string& oldEmail, const string& newEmail,
        string newPassword, const string& newFirstName,
        const string& newLastName, const string& newPhoneNumber,
        const string& newAddress, const string& newCity,
        const string& newZip) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        // Szyfrowanie has³a
        newPassword = xorEncryptDecrypt(newPassword, 'snpp');
        cout << "Aktualizowanie u¿ytkownika: " << oldEmail << endl;

        string sql = "UPDATE users SET email = ?, password = ?, firstname = ?, lastname = ?, "
            "phone_number = ?, address = ?, city = ?, zip = ? WHERE email = ?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, newEmail);
            pstmt->setString(2, newPassword);
            pstmt->setString(3, newFirstName);
            pstmt->setString(4, newLastName);
            pstmt->setString(5, newPhoneNumber);
            pstmt->setString(6, newAddress);
            pstmt->setString(7, newCity);
            pstmt->setString(8, newZip);
            pstmt->setString(9, oldEmail);

            // Wykonanie zapytania
            int result = pstmt->executeUpdate();
            return result > 0;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in updateUser(): " << e.what() << endl;
            return false;
        }
    }

    // Usuwa u¿ytkowników z bazy danych na podstawie listy ID
    bool deleteUser(const vector<int>& ids) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);

        // Najpierw usuñ wszystkie produkty i obrazy u¿ytkowników
        if (!deleteUserProducts(ids)) {
            return false;
        }

        // Usuwanie rekordów z tabel auction i sale, gdzie wystêpuje buyerID
        try {
            unique_ptr<sql::PreparedStatement> pstmt;
            for (const int id : ids) {
                // Usuñ z tabeli auction
                pstmt.reset(conn->prepareStatement("DELETE FROM auction WHERE buyerID = ?;"));
                pstmt->setInt(1, id);
                pstmt->executeUpdate();

                // Usuñ z tabeli sale
                pstmt.reset(conn->prepareStatement("DELETE FROM sale WHERE buyerID = ?;"));
                pstmt->setInt(1, id);
                pstmt->executeUpdate();
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteUser() - deleting from auction and sale: " << e.what() << endl;
            return false;
        }

        // Teraz usuwamy same konta u¿ytkowników
        string sql = "DELETE FROM users WHERE id IN (";
        for (size_t i = 0; i < ids.size(); ++i) {
            sql += "?";
            if (i < ids.size() - 1) sql += ",";
        }
        sql += ");";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            for (size_t i = 0; i < ids.size(); ++i) {
                pstmt->setInt(i + 1, ids[i]);
            }

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteUser(): " << e.what() << endl;
            return false;
        }
    }


    bool addCat(const string& name) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string Catsql = "INSERT INTO category (category_name) VALUES (?);";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(Catsql));
            pstmt->setString(1, name);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in addCat(): " << e.what() << endl;
            return false;
        }
    }


    bool updateCat(const string& oldName, const string& newName) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        cout << "Aktualizowanie kategorii: " << oldName << endl;

        string sql = "UPDATE category SET category_name = ? WHERE category_name = ?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, newName);
            pstmt->setString(2, oldName);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in updateCat(): " << e.what() << endl;
            return false;
        }
    }


    bool doesCatExist(const string& name) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string catsql = "SELECT * FROM category WHERE category_name = ?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(catsql));
            pstmt->setString(1, name);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Sprawdzenie, czy wynik zawiera jakiekolwiek wiersze
            return res->next();
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in doesCatExist(): " << e.what() << endl;
            return false;
        }
    }


    bool deleteCat(int id) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "DELETE FROM category WHERE id = ?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, id);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteCat(): " << e.what() << endl;
            return false;
        }
    }


    struct CategoryDetails {
        int id = -1;
        string category_name;
    };

    vector<CategoryDetails> getAllCategory() {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        vector<CategoryDetails> categories;
        string getCatSql = "SELECT id, category_name FROM category;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::Statement> stmt(conn->createStatement());

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(stmt->executeQuery(getCatSql));

            // Przetwarzanie wyników
            while (res->next()) {
                CategoryDetails cat;
                cat.id = res->getInt("id");
                cat.category_name = res->getString("category_name");
                categories.push_back(cat);
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getAllCategory(): " << e.what() << endl;
        }
        return categories;
    }



    struct UserDetails {
        int id = -1; // Ustawienie domyœlnej wartoœci
        string firstname;
        string lastname;
        string phone_number;
        string email;
        string login;
        string password;
        string address;
        string city;
        string zip;
        int role = 0;
    };


    // Pobiera wszystkich u¿ytkowników (oprócz admina) z bazy danych
    vector<UserDetails> getAllUsers() {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        vector<UserDetails> users;
        string getUsersSql = "SELECT id, firstname, lastname, email, phone_number, login, password, address, city, zip, role FROM users;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::Statement> stmt(conn->createStatement());

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(stmt->executeQuery(getUsersSql));

            // Przetwarzanie wyników
            while (res->next()) {
                UserDetails user;
                user.id = res->getInt("id");
                user.firstname = res->getString("firstname");
                user.lastname = res->getString("lastname");
                user.email = res->getString("email");
                user.phone_number = res->getString("phone_number");
                user.login = res->getString("login");
                // Deszyfrowanie przechowywanego has³a
                string encpassword = res->getString("password");
                string dencPasswd = xorEncryptDecrypt(encpassword, 'snpp');
                user.password = dencPasswd;
                user.address = res->getString("address");
                user.city = res->getString("city");
                user.zip = res->getString("zip");
                user.role = res->getInt("role");
                users.push_back(user);
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getAllUsers(): " << e.what() << endl;
        }
        return users;
    }

    UserDetails getUserDetailsById(int userId) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string getUserSql = "SELECT id, firstname, lastname, email, phone_number, login, password, address, city, zip, role FROM users WHERE id = ?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(getUserSql));
            pstmt->setInt(1, userId);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Przetwarzanie wyników
            if (res->next()) {
                UserDetails user;
                user.id = res->getInt("id");
                user.firstname = res->getString("firstname");
                user.lastname = res->getString("lastname");
                user.email = res->getString("email");
                user.phone_number = res->getString("phone_number");
                user.login = res->getString("login");
                string encpassword = res->getString("password");
                string decPasswd = xorEncryptDecrypt(encpassword, 'snpp');
                user.password = decPasswd;
                user.address = res->getString("address");
                user.city = res->getString("city");
                user.zip = res->getString("zip");
                user.role = res->getInt("role");
                return user;
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getUserDetailsById(): " << e.what() << endl;
        }
        return UserDetails{};
    }

    int getUserIdByLogin(const string& login) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string getUserSql = "SELECT id FROM users WHERE login = ?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(getUserSql));
            pstmt->setString(1, login);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Sprawdzenie, czy wynik zawiera jakiekolwiek wiersze i zwrócenie ID
            if (res->next()) {
                return res->getInt("id");
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getUserIdByLogin(): " << e.what() << endl;
        }
        return -1; // Zwraca -1, jeœli u¿ytkownik nie zostanie znaleziony
    }

    struct UserData {
        int id = -1;
        string firstname;
        string lastname;
        string email;
        string phone_number;
        string login;
        string password;
        string address;
        string city;
        string zip;
        int role = -1;
    };

    UserData getUserData(const string& login) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "SELECT id, firstname, lastname, email, phone_number, login, password, address, city, zip, role FROM users WHERE login = ?;";
        UserData data;

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, login);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Przetwarzanie wyników
            if (res->next()) {
                data.id = res->getInt("id");
                data.firstname = res->getString("firstname");
                data.lastname = res->getString("lastname");
                data.email = res->getString("email");
                data.phone_number = res->getString("phone_number");
                data.login = res->getString("login");
                string encryptedPassword = res->getString("password");
                string decryptedStoredPassword = xorEncryptDecrypt(encryptedPassword, 'snpp');
                data.password = decryptedStoredPassword;
                data.address = res->getString("address");
                data.city = res->getString("city");
                data.zip = res->getString("zip");
                data.role = res->getInt("role");
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getUserData(): " << e.what() << endl;
        }
        return data;
    }

    ////////////////////////////////// PRODUKTY
    int addImage(const string& name, const string& path) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "INSERT INTO Images (name, path) VALUES (?, ?);";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, name);
            pstmt->setString(2, path);

            // Wykonanie zapytania
            pstmt->executeUpdate();

            // Pobranie ID ostatnio dodanego rekordu
            unique_ptr<sql::Statement> stmt(conn->createStatement());
            unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID();"));
            if (res->next()) {
                return res->getInt(1);
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in addImage(): " << e.what() << endl;
        }
        return -1; // W przypadku b³êdu zwraca -1
    }


    bool linkRelations(int product_id, int image_id) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "INSERT INTO ProductImages (product_id, image_id) VALUES (?, ?);";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, product_id);
            pstmt->setInt(2, image_id);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in linkRelations(): " << e.what() << endl;
            return false;
        }
    }


    int addProduct(const string& product_name, const string& product_slug,
        const string& product_desc_long, const string& product_desc_short,
        double product_price, const string& product_date,
        const string& product_enddate, const string& product_delivery,
        double product_delivery_cost, int product_manager, int product_category,
        int buy_now, int auction) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = R"(
        INSERT INTO Products (
            product_name, product_slug, product_desc_long, product_desc_short, 
            product_price, product_date, product_enddate, 
            product_delivery, product_delivery_cost, product_manager, 
            product_category, buy_now, auction
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";


        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, product_name);
            pstmt->setString(2, product_slug);
            pstmt->setString(3, product_desc_long);
            pstmt->setString(4, product_desc_short);
            pstmt->setDouble(5, product_price);
            pstmt->setString(6, product_date);
            pstmt->setString(7, product_enddate);
            pstmt->setString(8, product_delivery);
            pstmt->setDouble(9, product_delivery_cost);
            pstmt->setInt(10, product_manager);
            pstmt->setInt(11, product_category);
            pstmt->setInt(12, buy_now);
            pstmt->setInt(13, auction);

            // Wykonanie zapytania
            pstmt->executeUpdate();

            // Pobranie ID ostatnio dodanego rekordu
            unique_ptr<sql::Statement> stmt(conn->createStatement());
            unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT LAST_INSERT_ID();"));
            if (res->next()) {
                return res->getInt(1);
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in addProduct(): " << e.what() << endl;
        }
        return -1; // W przypadku b³êdu zwraca -1
    }

    bool updateProduct(const string& product_slug, const string& product_name,
        const string& product_desc_long, const string& product_desc_short,
        double product_price, const string& product_date,
        const string& product_enddate, const string& product_delivery,
        double product_delivery_cost, int product_manager, int product_category,
        int buy_now, int auction, int archived) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = R"(
        UPDATE Products SET
            product_name = ?, product_desc_long = ?, product_desc_short = ?, 
            product_price = ?, product_date = ?, product_enddate = ?, 
            product_delivery = ?, product_delivery_cost = ?, product_manager = ?, 
            product_category = ?, buy_now = ?, auction = ?, archived = ?
        WHERE product_slug = ?;
    )";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, product_name);
            pstmt->setString(2, product_desc_long);
            pstmt->setString(3, product_desc_short);
            pstmt->setDouble(4, product_price);
            pstmt->setString(5, product_date);
            pstmt->setString(6, product_enddate);
            pstmt->setString(7, product_delivery);
            pstmt->setDouble(8, product_delivery_cost);
            pstmt->setInt(9, product_manager);
            pstmt->setInt(10, product_category);
            pstmt->setInt(11, buy_now);
            pstmt->setInt(12, auction);
            pstmt->setInt(13, archived);
            pstmt->setString(14, product_slug);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in updateProduct(): " << e.what() << endl;
            return false;
        }
    }

    bool doesProductExist(const string& product_slug) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string productsql = "SELECT * FROM Products WHERE product_slug = ?;";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(productsql));
            pstmt->setString(1, product_slug);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Sprawdzenie, czy wynik zawiera jakiekolwiek wiersze
            return res->next();
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in doesProductExist(): " << e.what() << endl;
            return false;
        }
    }

    struct ProductDetails {
        int id = -1;
        string name;
        string slug;
        string descLong;
        string descShort;
        double price = -1;
        string date;
        string endDate;
        string delivery;
        double deliveryCost = -1;
        int manager = -1;
        string categoryName;
        int buyNow = -1;
        int auction = -1;
        int archived = -1;
        vector<pair<string, string>> images;
    };

    vector<ProductDetails> getAllProducts() {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        vector<ProductDetails> products;
        string getProductSql = R"(
        SELECT 
            p.id, p.product_name, p.product_slug, p.product_desc_long, p.product_desc_short, 
            p.product_price, p.product_date, p.product_enddate, p.product_delivery, p.product_delivery_cost, 
            p.product_manager, c.category_name, p.buy_now, p.auction, p.archived, 
            i.name, i.path
        FROM Products p
        LEFT JOIN ProductImages pi ON p.id = pi.product_id
        LEFT JOIN Images i ON pi.image_id = i.id
        LEFT JOIN category c ON p.product_category = c.id
    )";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::Statement> stmt(conn->createStatement());

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(stmt->executeQuery(getProductSql));

            // Przetwarzanie wyników
            map<int, ProductDetails> tempProducts;
            while (res->next()) {
                int id = res->getInt("id");
                auto& product = tempProducts[id];
                product.id = id;
                product.name = res->getString("product_name");
                product.slug = res->getString("product_slug");
                product.descLong = res->getString("product_desc_long");
                product.descShort = res->getString("product_desc_short");
                product.price = res->getDouble("product_price");
                product.date = res->getString("product_date");
                product.endDate = res->getString("product_enddate");
                product.delivery = res->getString("product_delivery");
                product.deliveryCost = res->getDouble("product_delivery_cost");
                product.manager = res->getInt("product_manager");
                product.categoryName = res->getString("category_name");
                product.buyNow = res->getInt("buy_now");
                product.auction = res->getInt("auction");
                product.archived = res->getInt("archived");

                if (res->getString("name") != "" && res->getString("path") != "") {
                    product.images.emplace_back(res->getString("name"), res->getString("path"));
                }
            }

            // Przenoszenie produktów do finalnego wektora
            for (auto& pair : tempProducts) {
                products.push_back(std::move(pair.second));
            }

        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getAllProducts(): " << e.what() << endl;
        }

        return products;
    }


    struct UserProductDetails {
        int id = -1;
        string name;
        string slug;
        string descLong;
        string descShort;
        double price = -1;
        string date;
        string endDate;
        string delivery;
        double deliveryCost = -1;
        int manager = -1;
        string categoryName;
        int buyNow = -1;
        int auction = -1;
        int archived = -1;
        vector<pair<string, string>> images;

    };

    vector<UserProductDetails> getUserProducts(int user_id) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        vector<UserProductDetails> products;
        string getProductSql = R"(
        SELECT 
            p.id, p.product_name, p.product_slug, p.product_desc_long, p.product_desc_short, 
            p.product_price, p.product_date, p.product_enddate, p.product_delivery, p.product_delivery_cost, 
            p.product_manager, c.category_name, p.buy_now, p.auction, p.archived,
            i.name, i.path
        FROM Products p
        LEFT JOIN ProductImages pi ON p.id = pi.product_id
        LEFT JOIN Images i ON pi.image_id = i.id
        LEFT JOIN category c ON p.product_category = c.id
        WHERE p.product_manager = ?
    )";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(getProductSql));
            pstmt->setInt(1, user_id);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Przetwarzanie wyników
            map<int, UserProductDetails> tempProducts;
            while (res->next()) {
                int id = res->getInt("id");
                auto& product = tempProducts[id];
                product.id = id;
                product.name = res->getString("product_name");
                product.slug = res->getString("product_slug");
                product.descLong = res->getString("product_desc_long");
                product.descShort = res->getString("product_desc_short");
                product.price = res->getDouble("product_price");
                product.date = res->getString("product_date");
                product.endDate = res->getString("product_enddate");
                product.delivery = res->getString("product_delivery");
                product.deliveryCost = res->getDouble("product_delivery_cost");
                product.manager = res->getInt("product_manager");
                product.categoryName = res->getString("category_name");
                product.buyNow = res->getInt("buy_now");
                product.auction = res->getInt("auction");
                product.archived = res->getInt("archived");

                if (res->getString("name") != "" && res->getString("path") != "") {
                    product.images.emplace_back(res->getString("name"), res->getString("path"));
                }
            }

            // Przenoszenie produktów do finalnego wektora
            for (auto& pair : tempProducts) {
                products.push_back(std::move(pair.second));
            }

        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getUserProducts(): " << e.what() << endl;
        }

        return products;
    }

    // Struktura do przechowywania szczegó³ów produktu oraz powi¹zanych z nim obrazów
    struct ProductDetailsWithImages {
        int id = -1;
        string name;
        string slug;
        string descLong;
        string descShort;
        double price = -1;
        string date;
        string endDate;
        string delivery;
        double deliveryCost = -1;
        int manager = -1;
        string categoryName;
        int buyNow = -1;
        int auction = -1;
        int archived = -1;
        vector<pair<string, string>> images;
        int currentBidder = -1;
    };

    ProductDetailsWithImages getProductBySlug(const string& slug) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        ProductDetailsWithImages productDetails;
        productDetails.id = -1; // Ustawienie domyœlnej wartoœci

        string getProductSql = R"(
        SELECT 
            p.id, p.product_name, p.product_slug, p.product_desc_long, p.product_desc_short, 
            p.product_price, p.product_date, p.product_enddate, p.product_delivery, p.product_delivery_cost, 
            p.product_manager, c.category_name, p.buy_now, p.auction, p.archived,
            i.name, i.path 
        FROM Products p 
        LEFT JOIN ProductImages pi ON p.id = pi.product_id 
        LEFT JOIN Images i ON pi.image_id = i.id 
        LEFT JOIN category c ON p.product_category = c.id 
        WHERE p.product_slug = ?
    )";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(getProductSql));
            pstmt->setString(1, slug);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Przetwarzanie wyników
            while (res->next()) {
                productDetails.id = res->getInt("id");
                productDetails.name = res->getString("product_name");
                productDetails.slug = res->getString("product_slug");
                productDetails.descLong = res->getString("product_desc_long");
                productDetails.descShort = res->getString("product_desc_short");
                productDetails.price = res->getDouble("product_price");
                productDetails.date = res->getString("product_date");
                productDetails.endDate = res->getString("product_enddate");
                productDetails.delivery = res->getString("product_delivery");
                productDetails.deliveryCost = res->getDouble("product_delivery_cost");
                productDetails.manager = res->getInt("product_manager");
                productDetails.categoryName = res->getString("category_name");
                productDetails.buyNow = res->getInt("buy_now");
                productDetails.auction = res->getInt("auction");
                productDetails.archived = res->getInt("archived");

                if (res->getString("name") != "" && res->getString("path") != "") {
                    productDetails.images.emplace_back(res->getString("name"), res->getString("path"));
                }
            }

            // Sprawdzenie, czy istnieje rekord w tabeli 'auction' dla tego produktu
            if (productDetails.id != -1) {
                string auctionCheckSql = "SELECT buyerID FROM auction WHERE productID = ?";
                unique_ptr<sql::PreparedStatement> pstmt2(conn->prepareStatement(auctionCheckSql));
                pstmt2->setInt(1, productDetails.id);
                unique_ptr<sql::ResultSet> res2(pstmt2->executeQuery());

                if (res2->next()) {
                    productDetails.currentBidder = res2->getInt("buyerID");
                }
            }

        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getProductBySlug(): " << e.what() << endl;
        }

        return productDetails;
    }


    // Metoda do pobierania identyfikatorów obrazów powi¹zanych z produktem
    vector<int> getImageIds(int productId) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        vector<int> ids;
        string sql = "SELECT image_id FROM ProductImages WHERE product_id = ?";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, productId);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Przetwarzanie wyników
            while (res->next()) {
                ids.push_back(res->getInt("image_id"));
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getImageIds(): " << e.what() << endl;
        }
        return ids;
    }


    // Metoda do pobierania œcie¿ek obrazów powi¹zanych z produktem
    vector<string> getImagesPaths(int productId) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        vector<string> paths;
        string sql = R"(
        SELECT i.path FROM Images i
        JOIN ProductImages pi ON i.id = pi.image_id
        WHERE pi.product_id = ?
    )";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, productId);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Przetwarzanie wyników
            while (res->next()) {
                paths.push_back(res->getString("path"));
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getImagesPaths(): " << e.what() << endl;
        }
        return paths;
    }


    // Metoda do usuwania powi¹zañ obrazów z produktem
    bool deleteProductImages(int productId) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "DELETE FROM ProductImages WHERE product_id = ?";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, productId);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteProductImages(): " << e.what() << endl;
            return false;
        }
    }


    // Metoda do usuwania obrazów z tabeli Images
    bool deleteImages(const vector<int>& imageIds) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        if (imageIds.empty()) {
            return true; // Brak obrazów do usuniêcia
        }

        string sql = "DELETE FROM Images WHERE id IN (";
        for (size_t i = 0; i < imageIds.size(); ++i) {
            sql += "?";
            if (i < imageIds.size() - 1) {
                sql += ", ";
            }
        }
        sql += ");";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            for (size_t i = 0; i < imageIds.size(); ++i) {
                pstmt->setInt(i + 1, imageIds[i]);
            }

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteImages(): " << e.what() << endl;
            return false;
        }
    }


    // Metoda do usuwania produktu
    bool deleteProduct(int productId) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        try {
            // Usuwanie z tabeli 'sale'
            string deleteSaleSql = "DELETE FROM sale WHERE productID = ?";
            unique_ptr<sql::PreparedStatement> pstmtSale(conn->prepareStatement(deleteSaleSql));
            pstmtSale->setInt(1, productId);
            pstmtSale->executeUpdate();

            // Usuwanie z tabeli 'auction'
            string deleteAuctionSql = "DELETE FROM auction WHERE productID = ?";
            unique_ptr<sql::PreparedStatement> pstmtAuction(conn->prepareStatement(deleteAuctionSql));
            pstmtAuction->setInt(1, productId);
            pstmtAuction->executeUpdate();

            // Usuwanie z tabeli 'Products'
            string deleteProductSql = "DELETE FROM Products WHERE id = ?";
            unique_ptr<sql::PreparedStatement> pstmtProduct(conn->prepareStatement(deleteProductSql));
            pstmtProduct->setInt(1, productId);
            pstmtProduct->executeUpdate();

            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteProduct(): " << e.what() << endl;
            return false;
        }
    }

    bool deleteUserProducts(const vector<int>& userIds) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        try {
            // Przygotowanie listy identyfikatorów u¿ytkowników do zapytania
            string userIDsString;
            for (size_t i = 0; i < userIds.size(); ++i) {
                userIDsString += "?";
                if (i < userIds.size() - 1) userIDsString += ",";
            }

            // Pobieranie ID i œcie¿ek obrazów
            string sqlSelectImages = R"(
                SELECT id, path FROM Images WHERE id IN (
                    SELECT image_id FROM ProductImages WHERE product_id IN (
                        SELECT id FROM Products WHERE product_manager IN ( )" + userIDsString + R"( )
                    )
                )
            )";
            unique_ptr<sql::PreparedStatement> pstmtSelectImages(conn->prepareStatement(sqlSelectImages));
            for (size_t i = 0; i < userIds.size(); ++i) {
                pstmtSelectImages->setInt(i + 1, userIds[i]);
            }
            sql::ResultSet* resImages = pstmtSelectImages->executeQuery();

            vector<pair<int, string>> imageDetails;
            while (resImages->next()) {
                imageDetails.emplace_back(resImages->getInt("id"), resImages->getString("path"));
            }
            delete resImages;

            // Usuwanie powi¹zañ obrazów z produktami
            string sqlDeleteProductImages = "DELETE FROM ProductImages WHERE product_id IN "
                "(SELECT id FROM Products WHERE product_manager IN (" + userIDsString + "))";
            unique_ptr<sql::PreparedStatement> pstmtDeleteProductImages(conn->prepareStatement(sqlDeleteProductImages));
            for (size_t i = 0; i < userIds.size(); ++i) {
                pstmtDeleteProductImages->setInt(i + 1, userIds[i]);
            }
            pstmtDeleteProductImages->executeUpdate();

            // Usuwanie obrazów z tabeli Images
            for (const auto& image : imageDetails) {
                string sqlDeleteImage = "DELETE FROM Images WHERE id = ?";
                unique_ptr<sql::PreparedStatement> pstmtDeleteImage(conn->prepareStatement(sqlDeleteImage));
                pstmtDeleteImage->setInt(1, image.first);
                pstmtDeleteImage->executeUpdate();

                // Usuwanie plików obrazów z dysku
                if (remove(image.second.c_str()) != 0) {
                    cerr << "Error removing file: " << image.second << endl;
                }
            }

            // Usuwanie powi¹zanych rekordów z tabeli 'sale'
            string sqlDeleteSale = "DELETE FROM sale WHERE productID IN "
                "(SELECT id FROM Products WHERE product_manager IN (" + userIDsString + "))";
            unique_ptr<sql::PreparedStatement> pstmtDeleteSale(conn->prepareStatement(sqlDeleteSale));
            for (size_t i = 0; i < userIds.size(); ++i) {
                pstmtDeleteSale->setInt(i + 1, userIds[i]);
            }
            pstmtDeleteSale->executeUpdate();

            // Usuwanie powi¹zanych rekordów z tabeli 'auction'
            string sqlDeleteAuction = "DELETE FROM auction WHERE productID IN "
                "(SELECT id FROM Products WHERE product_manager IN (" + userIDsString + "))";
            unique_ptr<sql::PreparedStatement> pstmtDeleteAuction(conn->prepareStatement(sqlDeleteAuction));
            for (size_t i = 0; i < userIds.size(); ++i) {
                pstmtDeleteAuction->setInt(i + 1, userIds[i]);
            }
            pstmtDeleteAuction->executeUpdate();

            // Usuwanie produktów
            string sqlDeleteProducts = "DELETE FROM Products WHERE product_manager IN (" + userIDsString + ")";
            unique_ptr<sql::PreparedStatement> pstmtDeleteProducts(conn->prepareStatement(sqlDeleteProducts));
            for (size_t i = 0; i < userIds.size(); ++i) {
                pstmtDeleteProducts->setInt(i + 1, userIds[i]);
            }
            pstmtDeleteProducts->executeUpdate();

            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteUserProducts(): " << e.what() << endl;
            return false;
        }
    }



    // Metoda do znalezienia ID obrazu po nazwie
    int getImageIdByName(const string& imageName) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "SELECT id FROM Images WHERE name = ?";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, imageName);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Sprawdzenie, czy wynik zawiera jakiekolwiek wiersze
            if (res->next()) {
                return res->getInt("id");
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getImageIdByName(): " << e.what() << endl;
        }
        return -1;
    }


    // Metoda do usuwania pojedynczego powi¹zania obrazu z produktem
    bool deleteProductImage(int productId, int imageId) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "DELETE FROM ProductImages WHERE product_id = ? AND image_id = ?";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, productId);
            pstmt->setInt(2, imageId);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteProductImage(): " << e.what() << endl;
            return false;
        }
    }


    // Metoda do usuwania obrazu z tabeli Images
    bool deleteImage(int imageId) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "DELETE FROM Images WHERE id = ?";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, imageId);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in deleteImage(): " << e.what() << endl;
            return false;
        }
    }

    // Metoda do pobierania œcie¿ki obrazu
    string getImagePath(int imageId) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string path;
        string sql = "SELECT path FROM Images WHERE id = ?";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, imageId);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Sprawdzenie, czy wynik zawiera jakiekolwiek wiersze
            if (res->next()) {
                path = res->getString("path");
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getImagePath(): " << e.what() << endl;
        }
        return path;
    }


    bool addSale(int buyerID, int sellerID, int productID, const string& saleDate, const string& status) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        try {
            // Dodawanie informacji o sprzeda¿y
            string sqlSale = "INSERT INTO sale (buyerID, sellerID, productID, saleDate, status) VALUES (?, ?, ?, ?, ?)";
            unique_ptr<sql::PreparedStatement> pstmtSale(conn->prepareStatement(sqlSale));
            pstmtSale->setInt(1, buyerID);
            pstmtSale->setInt(2, sellerID);
            pstmtSale->setInt(3, productID);
            pstmtSale->setString(4, saleDate);
            pstmtSale->setString(5, status);
            pstmtSale->executeUpdate();

            // Aktualizacja produktu w tabeli Products
            string sqlProduct = "UPDATE Products SET archived = 1 WHERE id = ?";
            unique_ptr<sql::PreparedStatement> pstmtProduct(conn->prepareStatement(sqlProduct));
            pstmtProduct->setInt(1, productID);
            pstmtProduct->executeUpdate();

            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in addSale(): " << e.what() << endl;
            return false;
        }
    }


    bool addAuction(int buyerID, int sellerID, int productID, const string& saleDate, const string& status, double bid) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        try {
            // Sprawdzenie, czy rekord ju¿ istnieje
            string checkSql = "SELECT COUNT(*) FROM auction WHERE productID = ? AND sellerID = ?";
            unique_ptr<sql::PreparedStatement> pstmtCheck(conn->prepareStatement(checkSql));
            pstmtCheck->setInt(1, productID);
            pstmtCheck->setInt(2, sellerID);
            unique_ptr<sql::ResultSet> resCheck(pstmtCheck->executeQuery());

            int count = 0;
            if (resCheck->next()) {
                count = resCheck->getInt(1);
            }

            string sql;
            if (count > 0) {
                // Rekord ju¿ istnieje, wiêc aktualizuj
                sql = "UPDATE auction SET buyerID = ?, saleDate = ? WHERE productID = ? AND sellerID = ?";
            }
            else {
                // Rekord nie istnieje, wiêc dodaj nowy
                sql = "INSERT INTO auction (buyerID, sellerID, productID, saleDate, status) VALUES (?, ?, ?, ?, ?)";
            }

            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, buyerID);
            if (count > 0) {
                // Dla aktualizacji
                pstmt->setString(2, saleDate);
                pstmt->setInt(3, productID);
                pstmt->setInt(4, sellerID);
            }
            else {
                // Dla dodania nowego rekordu
                pstmt->setInt(2, sellerID);
                pstmt->setInt(3, productID);
                pstmt->setString(4, saleDate);
                pstmt->setString(5, status);
            }
            pstmt->executeUpdate();

            // Aktualizacja ceny produktu w tabeli 'Products'
            string updateProductSql = "UPDATE Products SET product_price = ? WHERE id = ?";
            unique_ptr<sql::PreparedStatement> pstmtUpdateProduct(conn->prepareStatement(updateProductSql));
            pstmtUpdateProduct->setDouble(1, bid);
            pstmtUpdateProduct->setInt(2, productID);
            pstmtUpdateProduct->executeUpdate();

            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in addAuction(): " << e.what() << endl;
            return false;
        }
    }


    struct ShoppingDetails {
        int saleId = -1;
        string sellerFirstName;
        string sellerLastName;
        string sellerPhoneNumber;
        string productName;
        string productSlug;
        double productPrice = -1;
        string productDelivery;
        double productDeliveryCost = -1;
        string buyerFirstName;
        string buyerLastName;
        string buyerPhoneNumber;
        string buyerAddress;
        string buyerCity;
        string buyerZip;
        string saleDate;
        string status;
    };


    vector<ShoppingDetails> getUserShopping(int user_id) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        vector<ShoppingDetails> shoppingDetailsList;

        string sql = R"(
        SELECT 
            s.id,
            seller.firstname, seller.lastname, seller.phone_number,
            p.product_name, p.product_slug, p.product_price, p.product_delivery, p.product_delivery_cost,
            buyer.firstname, buyer.lastname, buyer.phone_number, buyer.address, buyer.city, buyer.zip,
            s.saleDate, s.status
        FROM sale s
        JOIN users seller ON s.sellerID = seller.id
        JOIN users buyer ON s.buyerID = buyer.id
        JOIN Products p ON s.productID = p.id
        WHERE s.buyerID = ?
    )";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, user_id);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Przetwarzanie wyników
            while (res->next()) {
                ShoppingDetails details;
                details.saleId = res->getInt(1);
                details.sellerFirstName = res->getString(2);
                details.sellerLastName = res->getString(3);
                details.sellerPhoneNumber = res->getString(4);
                details.productName = res->getString(5);
                details.productSlug = res->getString(6);
                details.productPrice = res->getDouble(7);
                details.productDelivery = res->getString(8);
                details.productDeliveryCost = res->getDouble(9);
                details.buyerFirstName = res->getString(10);
                details.buyerLastName = res->getString(11);
                details.buyerPhoneNumber = res->getString(12);
                details.buyerAddress = res->getString(13);
                details.buyerCity = res->getString(14);
                details.buyerZip = res->getString(15);
                details.saleDate = res->getString(16);
                details.status = res->getString(17);

                shoppingDetailsList.push_back(details);
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getUserShopping(): " << e.what() << endl;
        }

        return shoppingDetailsList;
    }

    struct SalesDetails {
        int saleId = -1;
        string sellerFirstName;
        string sellerLastName;
        string sellerPhoneNumber;
        string productName;
        string productSlug;
        double productPrice = -1;
        string productDelivery;
        double productDeliveryCost = -1;
        string buyerFirstName;
        string buyerLastName;
        string buyerPhoneNumber;
        string buyerAddress;
        string buyerCity;
        string buyerZip;
        string saleDate;
        string status;
    };


    vector<SalesDetails> getUserSales(int user_id) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        vector<SalesDetails> salesDetailsList;

        string sql = R"(
        SELECT 
            s.id,
            seller.firstname, seller.lastname, seller.phone_number,
            p.product_name, p.product_slug, p.product_price, p.product_delivery, p.product_delivery_cost,
            buyer.firstname, buyer.lastname, buyer.phone_number, buyer.address, buyer.city, buyer.zip,
            s.saleDate, s.status
        FROM sale s
        JOIN users seller ON s.sellerID = seller.id
        JOIN users buyer ON s.buyerID = buyer.id
        JOIN Products p ON s.productID = p.id
        WHERE s.sellerID = ?
    )";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setInt(1, user_id);

            // Wykonanie zapytania
            unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

            // Przetwarzanie wyników
            while (res->next()) {
                SalesDetails details;
                details.saleId = res->getInt(1);
                details.sellerFirstName = res->getString(2);
                details.sellerLastName = res->getString(3);
                details.sellerPhoneNumber = res->getString(4);
                details.productName = res->getString(5);
                details.productSlug = res->getString(6);
                details.productPrice = res->getDouble(7);
                details.productDelivery = res->getString(8);
                details.productDeliveryCost = res->getDouble(9);
                details.buyerFirstName = res->getString(10);
                details.buyerLastName = res->getString(11);
                details.buyerPhoneNumber = res->getString(12);
                details.buyerAddress = res->getString(13);
                details.buyerCity = res->getString(14);
                details.buyerZip = res->getString(15);
                details.saleDate = res->getString(16);
                details.status = res->getString(17);

                salesDetailsList.push_back(details);
            }
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getUserSales(): " << e.what() << endl;
        }

        return salesDetailsList;
    }

    bool changeStatus(int saleID, const string& newStatus) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        string sql = "UPDATE sale SET status = ? WHERE id = ?";

        try {
            // Przygotowanie zapytania SQL
            unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
            pstmt->setString(1, newStatus);
            pstmt->setInt(2, saleID);

            // Wykonanie zapytania
            pstmt->executeUpdate();
            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in changeStatus(): " << e.what() << endl;
            return false;
        }
    }

    bool endAuction(int productID) {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        try {
            // Znajdowanie aukcji na podstawie productID
            string findAuctionSql = "SELECT id, buyerID, sellerID, saleDate FROM auction WHERE productID = ?";
            unique_ptr<sql::PreparedStatement> pstmtFind(conn->prepareStatement(findAuctionSql));
            pstmtFind->setInt(1, productID);
            unique_ptr<sql::ResultSet> resFind(pstmtFind->executeQuery());

            int buyerID, sellerID;
            string saleDate;
            if (resFind->next()) {
                buyerID = resFind->getInt("buyerID");
                sellerID = resFind->getInt("sellerID");
                saleDate = resFind->getString("saleDate");
            }
            else {
                return false; // Aukcja z tym productID nie istnieje
            }

            // Dodawanie informacji o sprzeda¿y do tabeli 'sale'
            string insertSaleSql = "INSERT INTO sale (buyerID, sellerID, productID, saleDate, status) VALUES (?, ?, ?, ?, 'Zakoñczona')";
            unique_ptr<sql::PreparedStatement> pstmtSale(conn->prepareStatement(insertSaleSql));
            pstmtSale->setInt(1, buyerID);
            pstmtSale->setInt(2, sellerID);
            pstmtSale->setInt(3, productID);
            pstmtSale->setString(4, saleDate);
            pstmtSale->executeUpdate();

            // Usuwanie aukcji z tabeli 'auction'
            string deleteAuctionSql = "DELETE FROM auction WHERE productID = ?";
            unique_ptr<sql::PreparedStatement> pstmtDelete(conn->prepareStatement(deleteAuctionSql));
            pstmtDelete->setInt(1, productID);
            pstmtDelete->executeUpdate();

            // Aktualizacja produktu w tabeli Products
            string updateProductSql = "UPDATE Products SET archived = 1 WHERE id = ?";
            unique_ptr<sql::PreparedStatement> pstmtUpdateProduct(conn->prepareStatement(updateProductSql));
            pstmtUpdateProduct->setInt(1, productID);
            pstmtUpdateProduct->executeUpdate();

            return true;
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in endAuction(): " << e.what() << endl;
            return false;
        }
    }

    struct MainData {
        int totalUsers = -1;
        int activeProducts = -1;
        int inactiveProducts = -1;
        int totalProducts = -1;
        int totalAuctions = -1;
        int totalSales = -1;
        int totalImages = -1;
        long long totalImagesSize = -1; // Rozmiar w bajtach
    };

    MainData getMainData() {
        unique_ptr<sql::Connection> conn;
        conn = unique_ptr<sql::Connection>(driver->connect(host_db, username_db, password_db));
        conn->setSchema(dbname);
        MainData data;

        try {
            unique_ptr<sql::Statement> stmt(conn->createStatement());

            // Iloœæ u¿ytkowników
            unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT COUNT(*) FROM users"));
            if (res->next()) {
                data.totalUsers = res->getInt(1);
            }

            // Iloœæ aktywnych produktów
            res.reset(stmt->executeQuery("SELECT COUNT(*) FROM Products WHERE archived = 0"));
            if (res->next()) {
                data.activeProducts = res->getInt(1);
            }

            // Iloœæ nieaktywnych produktów
            res.reset(stmt->executeQuery("SELECT COUNT(*) FROM Products WHERE archived = 1"));
            if (res->next()) {
                data.inactiveProducts = res->getInt(1);
            }

            // Iloœæ produktów ³¹cznie
            res.reset(stmt->executeQuery("SELECT COUNT(*) FROM Products"));
            if (res->next()) {
                data.totalProducts = res->getInt(1);
            }

            // Iloœæ licytacji
            res.reset(stmt->executeQuery("SELECT COUNT(*) FROM auction"));
            if (res->next()) {
                data.totalAuctions = res->getInt(1);
            }

            // Iloœæ sprzedanych produktów
            res.reset(stmt->executeQuery("SELECT COUNT(*) FROM sale"));
            if (res->next()) {
                data.totalSales = res->getInt(1);
            }

            // Iloœæ zdjêæ
            res.reset(stmt->executeQuery("SELECT COUNT(*) FROM Images"));
            if (res->next()) {
                data.totalImages = res->getInt(1);
            }

            // Rozmiar zdjêæ na dysku
            data.totalImagesSize = calculateTotalImageSize("static/uploads");
        }
        catch (sql::SQLException& e) {
            cerr << "SQLException in getMainData(): " << e.what() << endl;
        }

        return data;
    }

    long long calculateTotalImageSize(const string& directoryPath) {
        long long totalSize = 0;
        try {
            for (const auto& entry : filesystem::directory_iterator(directoryPath)) {
                if (entry.is_regular_file()) {
                    totalSize += filesystem::file_size(entry.path());
                }
            }
        }
        catch (const filesystem::filesystem_error& e) {
            cerr << "FileSystem Error: " << e.what() << endl;
        }
        return totalSize;
    }


};
