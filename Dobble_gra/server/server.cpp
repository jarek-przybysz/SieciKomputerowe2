#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <map>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "../json/include/nlohmann/json.hpp"
#include <random>

#define PORT 8080

using json = nlohmann::json;

// Struktura wiadomości wymienianej między klientem a serwerem
struct GameMessage
{
    char playerName[50];
    int cardID;
    int tablecardid;
    char chosenSymbol[50];
    char cardSymbols[8][50];
    int score;
    int lobby;
};

// Struktura karty
struct Card
{
    int id;
    std::vector<std::string> symbols;
};

// Globalne zmienne
std::vector<Card> cards;                      // Główna talia kart wczytana z JSON
std::map<int, std::vector<Card>> lobbyDecks;  // Talia dla każdego lobby
std::map<int, Card> tableCards;               // Karta na stole dla każdego lobby
std::map<std::string, int> playerScores;      // Wyniki graczy
std::map<int, Card> playerCards;              // Karty graczy
std::map<int, std::vector<int>> lobbyClients; // Klienci w każdym lobby
std::vector<int> clientSockets;               // Lista wszystkich połączeń klientów
bool gameStarted[3] = {false, false, false};  // Stan gry dla każdego lobby

// Funkcja do wczytania kart z pliku JSON
void loadCardsFromJSON(const std::string &filename)
{
    std::ifstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Nie można otworzyć pliku JSON." << std::endl;
        exit(EXIT_FAILURE);
    }

    json jsonData;
    file >> jsonData;

    if (!jsonData.contains("cards") || !jsonData["cards"].is_array())
    {
        std::cerr << "Niepoprawny format pliku JSON. Oczekiwano tablicy w polu 'cards'." << std::endl;
        exit(EXIT_FAILURE);
    }

    for (const auto &cardData : jsonData["cards"])
    {
        Card card;
        card.id = cardData.at("id").get<int>();
        for (const auto &symbol : cardData.at("symbols"))
        {
            card.symbols.push_back(symbol.get<std::string>());
        }
        cards.push_back(card);
        std::cout << "Wczytano kartę o ID: " << card.id << std::endl;
    }

    std::cout << "Wczytano " << cards.size() << " kart." << std::endl;
}

// Tasowanie kart w talii danego lobby
void shuffleLobbyDeck(int lobbyID)
{
    std::random_device rd;
    std::mt19937 g(rd());

    {
        std::shuffle(lobbyDecks[lobbyID].begin(), lobbyDecks[lobbyID].end(), g);
    }

    std::cout << "Karty w lobby " << lobbyID << " zostały potasowane." << std::endl;
}

// Inicjalizacja talii dla danego lobby
void initializeLobbyDeck(int lobbyID)
{
    {
        lobbyDecks[lobbyID] = cards; // Kopiowanie głównej talii
    }
    shuffleLobbyDeck(lobbyID);
    std::cout << "Talia dla lobby " << lobbyID
              << " zainicjalizowana. Liczba kart: "
              << lobbyDecks[lobbyID].size() << std::endl;
}

void endGame(int lobbyID)
{
    // Znajdź gracza z najwyższym wynikiem
    std::string winner;
    int maxScore = -1;
    for (int clientSocket : lobbyClients[lobbyID])
    {
        for (auto &[playerName, score] : playerScores)
        {
            if (score > maxScore)
            {
                maxScore = score;
                winner = playerName;
            }
            score = 0;
        }
    }

    // Wiadomość o zakończeniu gry
    GameMessage endMessage = {};
    endMessage.score = maxScore;
    snprintf(endMessage.playerName, sizeof(endMessage.playerName), "%s", winner.c_str());
    endMessage.tablecardid = -1; // Koniec gry
    // Wiadomość do klientów w lobby
    for (int clientSocket : lobbyClients[lobbyID])
    {
        send(clientSocket, &endMessage, sizeof(endMessage), 0);
    }

    std::cout << "Gra w lobby " << lobbyID << " zakończona! Wygral gracz: " << winner
              << " z wynikiem: " << maxScore << "." << std::endl;
    {
        lobbyDecks.erase(lobbyID);    // Usuń talię
        lobbyClients.erase(lobbyID);  // Usuń klientów
        tableCards.erase(lobbyID);    // Usuń kartę stołową
        gameStarted[lobbyID] = false; // Resetuj stan gry
    }
    
    // Resetuj talię dla nowej gry
    initializeLobbyDeck(lobbyID);
}

// Losowanie karty z talii danego lobby
Card drawCardFromLobby(int lobbyID)
{
    std::cout << "Rozpoczynam losowanie karty w lobby " << lobbyID
              << ". Liczba kart w talii: " << lobbyDecks[lobbyID].size() << std::endl;

    if (lobbyDecks[lobbyID].empty())
    {
        endGame(lobbyID);
    }

    else
    {
        Card drawnCard = lobbyDecks[lobbyID].back();
        lobbyDecks[lobbyID].pop_back();

        std::cout << "Wylosowano kartę o ID: " << drawnCard.id << " w lobby " << lobbyID << std::endl;
        return drawnCard;
    }
}

// Rozpoczęcie gry w lobby
void startGame(int lobbyID)
{
    std::cout << "Rozpoczęcie gry w lobby " << lobbyID << std::endl;

    {
        if (lobbyDecks[lobbyID].empty())
        {
            std::cerr << "Błąd: Brak kart w talii lobby " << lobbyID << " podczas startu gry!" << std::endl;
            return;
        }
        gameStarted[lobbyID] = true;
        tableCards[lobbyID] = drawCardFromLobby(lobbyID); // Karta na stole

        std::cout << "Karta stołowa w lobby " << lobbyID
                  << " ID: " << tableCards[lobbyID].id
                  << " z symbolami: ";
        for (const auto &symbol : tableCards[lobbyID].symbols)
        {
            std::cout << symbol << " ";
        }
        std::cout << std::endl;
    }

    // Wyślij karty graczom
    for (int clientSocket : lobbyClients[lobbyID])
    {

        GameMessage message;
        message.tablecardid = tableCards[lobbyID].id; // wiadomosc o karcie na stole
        Card playerCard = drawCardFromLobby(lobbyID);
        playerCards[clientSocket] = playerCard; // zapisanie informacji o karcie gracza na serwerze
        message.cardID = playerCard.id;         // wiadomosc karta w rece

        send(clientSocket, &message, sizeof(message), 0);
    }
}

// Obsługa klienta
void handleClient(int clientSocket)
{
    GameMessage message;

    int valread = recv(clientSocket, &message, sizeof(message), 0);
    if (valread <= 0)
    {
        std::cerr << "Błąd połączenia z klientem. Nie odebrano danych." << std::endl;
        close(clientSocket);
        return;
    }

    std::string playerName = message.playerName;
    playerScores[playerName] = 0;

    int chosenLobby = message.lobby;

    {
        // Sprawdzenie czy gra w wybranym lobby już trwa
        if (gameStarted[chosenLobby])
        {
            std::cout << "Gra w lobby " << chosenLobby << " już trwa. Gracz "
                      << playerName << " nie może dołączyć." << std::endl;
            close(clientSocket);
            return;
        }

        // Jeśli lobby nie istnieje, tworzymy je
        if (lobbyClients.find(chosenLobby) == lobbyClients.end())
        {
            std::cout << "Tworzenie nowego lobby: " << chosenLobby << std::endl;
            lobbyClients[chosenLobby] = {};
            initializeLobbyDeck(chosenLobby);
        }

        // Dodaj gracza do lobby
        lobbyClients[chosenLobby].push_back(clientSocket);
    }

    std::cout << "Gracz " << playerName << " dołączył do lobby " << chosenLobby
              << ". Liczba klientów: " << lobbyClients[chosenLobby].size() << std::endl;

    {
        // Uruchomienie gry, jeśli warunki są spełnione
        if (!gameStarted[chosenLobby] && lobbyClients[chosenLobby].size() >= 2)
        {
            std::cout << "Startowanie gry w lobby " << chosenLobby << std::endl;
            startGame(chosenLobby);
        }
    }

    while (true)
    {
        valread = recv(clientSocket, &message, sizeof(message), 0);
        if (valread <= 0)
        {
            std::cout << "Gracz " << playerName << " rozłączył się." << std::endl;
            {
                auto &clients = lobbyClients[chosenLobby];
                clients.erase(std::remove(clients.begin(), clients.end(), clientSocket), clients.end());

                std::cout << "Aktualna liczba klientów w lobby " << chosenLobby << ": "
                          << clients.size() << std::endl;

                // Usuń lobby, jeśli jest puste
                if (clients.empty())
                {
                    lobbyClients.erase(chosenLobby);
                    lobbyDecks.erase(chosenLobby);
                    tableCards.erase(chosenLobby);
                    gameStarted[chosenLobby] = false;
                    std::cout << "Lobby " << chosenLobby << " zostało usunięte, ponieważ nie ma graczy."
                              << std::endl;
                }
            }
            break;
        }

        std::string chosenSymbol = message.chosenSymbol;

        bool match = false;
        {
            Card &playerCard = playerCards[clientSocket];
            Card &tableCard = tableCards[chosenLobby];

            auto playerPos = std::find(playerCard.symbols.begin(), playerCard.symbols.end(), chosenSymbol);
            auto tablePos = std::find(tableCard.symbols.begin(), tableCard.symbols.end(), chosenSymbol);

            if (playerPos != playerCard.symbols.end() && tablePos != tableCard.symbols.end())
            {
                match = true;
            }
        }

        if (match)
        {
            playerScores[playerName]++;

            {
                playerCards[clientSocket] = tableCards[chosenLobby];
                tableCards[chosenLobby] = drawCardFromLobby(chosenLobby);
            }

            std::cout << "Gracz " << playerName << " zdobył punkt!" << std::endl;

            for (int socket : lobbyClients[chosenLobby])
            {
                GameMessage message;
                message.tablecardid = tableCards[chosenLobby].id; // Ustawienie nowej karty na stole
                message.cardID = playerCards[socket].id;          // Karta przypisana do danego gracza

                send(socket, &message, sizeof(message), 0);
            }
        }
    }

    close(clientSocket);
}

// Funkcja główna serwera
int main()
{
    loadCardsFromJSON("cards.json");

    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
    {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    std::cout << "Serwer uruchomiony. Oczekiwanie na połączenia..." << std::endl;

    std::fill(std::begin(gameStarted), std::end(gameStarted), false);

    while (true)
    {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
        {
            perror("Accept failed");
            continue;
        }

        std::thread clientThread(handleClient, new_socket);
        clientThread.detach();
    }

    return 0;
}
