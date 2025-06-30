document.addEventListener('DOMContentLoaded', () => {

    // --- STATE MANAGEMENT ---
    let currentUser = null;
    let liveGamesOffset = 0;

    // --- DOM ELEMENTS ---
    const authLinks = document.getElementById('auth-links');
    const profileSection = document.getElementById('profile-section');
    const profileIcon = document.getElementById('profile-icon');
    const profileDropdown = document.getElementById('profile-dropdown');
    const viewProfileLink = document.getElementById('view-profile-link');
    const logoutButton = document.getElementById('logout-button');
    const playButton = document.getElementById('play-button');
    const loginButton = document.getElementById('loginBtn');
    const signupButton = document.getElementById('signupBtn');

    const liveGamesList = document.getElementById('live-games-list');
    const refreshLiveGamesBtn = document.getElementById('refresh-live-games');
    const loadMoreLiveGamesBtn = document.getElementById('load-more-live-games');

    const gameHistorySection = document.getElementById('game-history-section');
    const gameHistoryList = document.getElementById('game-history-list');
    const showMoreHistoryLink = document.getElementById('show-more-history');

    const fetchData = async (path) => {
        try {
            const response = await fetch(path, {
                method: 'GET',
                credentials: 'include', // Include cookies if needed for auth
                headers: {
                    'Content-Type': 'application/json'
                }
            });

            if (!response.ok) {
                throw new Error(`HTTP error! Status: ${response.status}`);
            }

            const data = await response.json();
            return data;

        } catch (error) {
            console.error(`Failed to fetch ${path}:`, error);
            return null;
        }
    };


    // --- INITIALIZATION ---
    async function initializePage() {
        try {
            // const response = await fetch('/api/user/status');
            const userData = await fetchData('/api/user/status');

            if (userData.loggedIn) {
                currentUser = userData;
                setupLoggedInView();
            } else {
                setupLoggedOutView();
            }

            // Always load live games
            fetchAndRenderLiveGames();

        } catch (error) {
            console.error("Initialization Error:", error);
            setupLoggedOutView(); // Fallback to logged-out view on error
        }
    }

    // --- UI SETUP FUNCTIONS ---
    function setupLoggedInView() {
        // Nav bar
        authLinks.classList.add('hidden');
        profileSection.classList.remove('hidden');
        profileIcon.title = currentUser.username; // Hover text
        viewProfileLink.href = `/player/${currentUser.username}`;
        
        // Play button
        playButton.textContent = 'Play Game (5 min)';
        
        // Game History
        gameHistorySection.classList.remove('hidden');
        showMoreHistoryLink.href = `/player/${currentUser.username}`;
        fetchAndRenderGameHistory();
    }

    function setupLoggedOutView() {
        // Nav bar
        authLinks.classList.remove('hidden');
        profileSection.classList.add('hidden');
        
        // Play button
        playButton.textContent = 'Play Random Game (5 min)';

        // Game History
        gameHistorySection.classList.add('hidden');
    }


    // --- DATA FETCHING AND RENDERING ---

    async function fetchAndRenderLiveGames(path = '/live-games') {
        liveGamesList.innerHTML = '<p>Loading live games...</p>';
        try {
            // const response = await fetch(path);
            const games = await fetchData(path); // Using mock
            
            renderLiveGames(games.games);
        } catch (error) {
            console.error('Failed to fetch live games:', error);
            liveGamesList.innerHTML = '<p>Could not load live games.</p>';
        }
    }

    function renderLiveGames(games) {
        if (!games || games.length === 0) {
            console.log("No live games");
            liveGamesList.innerHTML = '<p>No live games currently.</p>';
            return;
        }
        liveGamesList.innerHTML = ''; // Clear current content
        games.forEach(game => {
            const gameElement = document.createElement('a');
            gameElement.className = 'live-game-item';
            gameElement.href = `/game/${game.id}`;
            gameElement.innerHTML = `
                <div class="live-game-players">
                    <span>⚪ <a href="/player/${game.white}" onclick="event.stopPropagation()">${game.white}</a></span>
                    <span>⚫ <a href="/player/${game.black}" onclick="event.stopPropagation()">${game.black}</a></span>
                </div>
            `;
            liveGamesList.appendChild(gameElement);
        });
    }

    async function fetchAndRenderGameHistory() {
        gameHistoryList.innerHTML = '<p>Loading history...</p>';
        try {
            // const response = await fetch('/games');
            const data = await fetchData('/games'); // Using mock
            renderGameHistory(data.games);
        } catch (error) {
            console.error('Failed to fetch game history:', error);
            gameHistoryList.innerHTML = '<p>Could not load game history.</p>';
        }
    }

    function renderGameHistory(games) {
        if (!games || games.length === 0) {
            gameHistoryList.innerHTML = '<p>No games played yet.</p>';
            return;
        }
        gameHistoryList.innerHTML = '';
        games.forEach(game => {
            let resultText = '';
            if (game.result === 'white') resultText = '1 - 0';
            else if (game.result === 'black') resultText = '0 - 1';
            else if (game.result === 'draw') resultText = '½ - ½';

            const itemElement = document.createElement('div');
            itemElement.className = 'history-grid history-item';
            itemElement.innerHTML = `
                <div class="game-col">
                    <a href="/player/${game.white}">${game.white}</a> vs <a href="/player/${game.black}">${game.black}</a>
                </div>
                <div class="result-col">${resultText}</div>
                <div class="date-col">${game.created_at}</div>
            `;
            gameHistoryList.appendChild(itemElement);
        });
    }


    function handleAuth(path) {
        const username = prompt("Enter username:");
        if (!username) return;

        const password = prompt("Enter password:");
        if (!password) return;

        const body = new URLSearchParams({ username, password });

        fetch(path, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded'
            },
            credentials: 'include', // send cookies
            body: body.toString()
        })
        .then(res => {
            if (!res.ok) {
                return res.text().then(text => {
                    throw new Error("Authentication failed: " + text);
                });
            }
            
            return res.json(); // assuming server responds with { username: "..." }
        })
        .then(data => {
            // Hide auth links
            document.getElementById('auth-links').classList.add('hidden');

            // Show profile section
            const profileSection = document.getElementById('profile-section');
            profileSection.classList.remove('hidden');

            // Set profile icon and username
            const profileIcon = document.getElementById('profile-icon');
            profileIcon.src = "https://www.gravatar.com/avatar/?d=mp&s=40";

            // Add username next to icon
            let usernameSpan = document.getElementById('profile-username');
            if (!usernameSpan) {
                usernameSpan = document.createElement('span');
                usernameSpan.id = 'profile-username';
                usernameSpan.style.marginLeft = '8px';
                profileIcon.parentElement.appendChild(usernameSpan);
            }
            usernameSpan.textContent = data.username;
        })
        .catch(err => {
            alert(err.message);
        });
    }


    // --- EVENT LISTENERS ---
    profileIcon.addEventListener('click', () => {
        profileDropdown.classList.toggle('hidden');
    });

    loginButton.addEventListener('click', () => {
        handleAuth('/login');
    });

    signupButton.addEventListener('click', () => {
        handleAuth('/signup');
    });

    logoutButton.addEventListener('click', (e) => {
        // Clear the session cookie
        document.cookie = "session_token=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;";
        // Reload the page
        window.location.reload();
    });

    refreshLiveGamesBtn.addEventListener('click', () => {
        liveGamesOffset = 0; // Reset offset on refresh
        fetchAndRenderLiveGames();
    });

    loadMoreLiveGamesBtn.addEventListener('click', () => {
        liveGamesOffset += 10;
        fetchAndRenderLiveGames(`/live-games?count=${liveGamesOffset}`);
    });

    // Close dropdown if clicked outside
    document.addEventListener('click', (e) => {
        if (!profileSection.contains(e.target)) {
            profileDropdown.classList.add('hidden');
        }
    });

    // --- START THE APP ---
    initializePage();

});
