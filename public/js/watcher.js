// fetch details of the ongoing game
// update each move
// let watcher make moves and reset the board to the actual game when a new move is received
// live chat
// add game result to live chat
// game pop-up when game is over
// game pop-up should disapper when rematch starts
// rematch feature

document.addEventListener("DOMContentLoaded", function () {

    let board = null;
    let socket = null;

    // Game state is now managed manually, based on server messages
    let currentTurn = 'w';
    let tinkerTurn = null;
    let isGameOver = false;
    let gameID = "";
    let whiteName = "";
    let blackName = "";
    let myName = null;
    let promotionSquare = null;
    let enpassantSquare = null;
    let isBottomWhite = true;

    // History for move navigation, stores FEN strings
    let moveHistory = ['start']; // Start with initial FEN
    let currentMoveIndex = 0; // Index for the moveHistory array
    let timeStamps = [];

    // Timers
    let whiteTimerInterval, blackTimerInterval;
    let whiteTime = 0;
    let blackTime = 0;

    let lastHighlighted = [];
    let lastHighlightedMoveCell = null;

    // --- DOM Elements ---
    const movesTbody = document.getElementById('moves-tbody');
    const prevBtn = document.getElementById('prev-btn');
    const nextBtn = document.getElementById('next-btn');
    const gameOverModal = document.getElementById('game-over-modal');
    const chatInput = document.getElementById('chat-input');
    const sendChatBtn = document.getElementById('send-chat-btn');

    // --- WebSocket Logic ---
    function initWebSocket() {
        const path = window.location.pathname;
        gameID = path.slice(path.lastIndexOf('/') + 1);

        socket = new WebSocket(`ws://10.123.22.94:8080/watch?topic=${gameID}`);

        socket.onopen = function(e) {
            console.log("[open] Connection established");
            addChatMessage("System", "Connected to game server.");
        };

        socket.onmessage = function(event) {
            console.log(`[message] Data received from server: ${event.data}`);
            handleServerMessage(event.data);
        };

        socket.onclose = function(event) {
            if (event.wasClean) {
                console.log(`[close] Connection closed cleanly, code=${event.code} reason=${event.reason}`);
            } else {
                console.log('[close] Connection died');
                if(!isGameOver) showOverlay("Connection lost, please check your connection and refresh the page to continue playing");
            }

            addChatMessage("System", "Connection lost.");
            stopTimers();
        };

        socket.onerror = function(error) {
            console.log(`[error] ${error.message}`);
            addChatMessage("System", "A connection error occurred.");
        };
    }

    function handleServerMessage(message) {
        const msgJson = JSON.parse(message);
        const type = msgJson.type;

        switch (type) {
            case 'info':
                whiteName = msgJson.white;
                blackName = msgJson.black;

                document.getElementById("bottom-name").innerText = whiteName;
                document.getElementById("top-name").innerText = blackName;

                whiteTime = parseInt(msgJson.whiteTime, 10);
                blackTime = parseInt(msgJson.blackTime, 10);

                board.orientation('white');

                if(msgJson.moves.length) {
                    const moves = msgJson.moves.split(' ');

                    moves.forEach((move, idx) => {
                        makeMove(move, false);
                        updateMoveHistory(move, idx%2 == 0 ? "w" : "b", board.fen())
                    });

                    currentTurn = moves.length % 2 ? 'b' : 'w';
                }

                if(msgJson.timeStamps.length) {
                    const stamps = msgJson.timeStamps.split(' ');

                    stamps.forEach((stamp, idx) => {
                        console.log(stamp);
                        timeStamps.push([parseInt(stamp.substring(0, stamp.indexOf('-') + 1)), parseInt(stamp.substring(stamp.indexOf('-') + 1, stamp.length))]);
                    });
                }

                updateTimersDisplay();
                
                if(moveHistory.length % 2 == 0) startBlackTimer();
                else startWhiteTimer();

                addChatMessage("System", `${whiteName} vs ${blackName}`);
                break;

            case 'rematch':
                gameID = msgJson.newGameID;
                socket.send(`rematch ${gameID}`);

                // Change URL from /game to /game/:id without reload
                history.replaceState({ gameID }, '', `/game/${gameID}`);

                break;

            case 'newmove': // A move was made (opponent)
                
                board.position(moveHistory[moveHistory.length - 1], false);
                currentMoveIndex = moveHistory.length - 1;

                let wsec = parseInt(msgJson.whiteSec, 10);
                let wnsec = parseInt(msgJson.whiteNanoSec, 10);
                let bsec = parseInt(msgJson.blackSec, 10);
                let bnsec = parseInt(msgJson.blackNanoSec, 10);

                let wTimeMs = wsec * 1000 + wnsec / 1e6;
                let bTimeMs = bsec * 1000 + bnsec / 1e6;

                wTimeMs = Math.ceil(wTimeMs/1000);
                bTimeMs = Math.ceil(bTimeMs/1000);

                updateTimersFromServer(wTimeMs, bTimeMs);
                timeStamps.push([wTimeMs, bTimeMs]);

                makeMove(msgJson.move, false);
                updateMoveHistory(msgJson.move, currentTurn, board.fen());

                changeTurn();
                handleTurnTimer();
                break;
            
            case 'result': // Game has ended
                isGameOver = true;
                stopTimers();
                handleGameResult(msgJson.result, msgJson.reason, msgJson.whiteScore, msgJson.blackScore);
                break;

            case 'chat':
                addChatMessage(msgJson.sender, msgJson.send);
                break;
        }
    }

    function isCastle(move, madeOnBoard) {
        const from = move.substring(0, 2);
        const to = move.substring(2, 4);

        const position = board.position();
        const piece = madeOnBoard ? position[to] :position[from];

        if(!piece || piece[1].toLowerCase() !== 'k') return false;

        if (from === 'e1' && to === 'g1') {
            if(!madeOnBoard) board.move('e1-g1', 'h1-f1');
            else board.move('h1-f1');

            return true;
        } else if (from === 'e1' && to === 'c1') {
            if(!madeOnBoard) board.move('e1-c1', 'a1-d1');
            else board.move('a1-d1');

            return true;
        } else if (from === 'e8' && to === 'g8') {
            if(!madeOnBoard) board.move('e8-g8', 'h8-f8');
            else board.move('h8-f8');

            return true;
        } else if (from === 'e8' && to === 'c8') {
            if(!madeOnBoard) board.move('e8-c8', 'a8-d8');
            else board.move('a8-d8');

            return true;
        }

        return false;
    }


    // --- Chessboard.js Logic ---
    function onDragStart(source, piece) {
        tinkerTurn = currentMoveIndex % 2 ? 'b' : 'w';
        
        // Check piece color against whose turn it is
        const pieceColor = piece.charAt(0);
        if (pieceColor !== tinkerTurn) return false;

        // disable scroll
        document.body.style.overflow = 'hidden';
    }

    function onDrop(source, target) {
        // enable scroll
        document.body.style.overflow = '';

        if(source == target || target == 'offBoard') return;

        // enagle scroll
        document.body.style.overflow = '';
        
        tinkerTurn = (tinkerTurn == 'w' ? 'b' : 'w');
    }

    function makeMove(move, madeOnBoard) {
        let source = move.substring(0, 2);
        let target = move.substring(2, 4);

        let position = board.position();
        const piece = madeOnBoard ? position[target] : position[source];
        const color = piece.charAt(0);

        if(move.length == 5) {
            delete position[source];
            position[target] =  color + move[4].toUpperCase();

            board.position(position, false);
        }
        else if(isCastle(move, madeOnBoard)) {
            board.position(board.position(), false);
        }
        else if(piece.charAt(1) === 'P' && target == enpassantSquare) {
            let rank = parseInt(target[1], 10);
            let captureSquare = target[0] + (color == 'w' ? (rank - 1) : (rank + 1));

            delete position[captureSquare];
            delete position[source];
            position[target] = piece;

            board.position(position, false);
        }
        else {
            board.move(`${move.substring(0, 2)}-${move.substring(2, 4)}`);
            board.position(board.position(), false);
        }

        // update enpassant square
        if(piece.charAt(1) === 'P' && Math.abs(parseInt(source[1]) - parseInt(target[1])) === 2) {
            const middleRank = (parseInt(source[1]) + parseInt(target[1])) / 2;
            enpassantSquare = target[0] + middleRank;
        }
        else enpassantSquare = null;
    }


    function changeTurn() {
        if(currentTurn === 'w') currentTurn = 'b';
        else currentTurn = 'w';
    }

    // --- UI Update Functions ---
    function updateMoveHistory(san, color, fen) {
        // Add the new FEN to our history for navigation
        moveHistory.push(fen);
        currentMoveIndex = moveHistory.length - 1;

        const moveNumber = Math.ceil(moveHistory.length / 2);

        if (color === 'w') {
            const newRow = document.createElement('tr');
            newRow.innerHTML = `<td class="p-1">${moveNumber}.</td><td class="p-1 font-semibold cursor-pointer" data-move-index="${currentMoveIndex}">${san}</td><td class="p-1 font-semibold"></td>`;
            movesTbody.appendChild(newRow);
        } else {
            const lastRow = movesTbody.lastElementChild;
            if(lastRow && lastRow.children.length > 2) {
                lastRow.children[2].textContent = san;
                lastRow.children[2].classList.add('cursor-pointer');
                lastRow.children[2].dataset.moveIndex = currentMoveIndex;
            }
        }
        movesTbody.parentElement.scrollTop = movesTbody.parentElement.scrollHeight;
        updateNavButtons();
        highlightMove(san.substring(0,2), san.substring(2, 4));
        highlightMoveNotation(currentMoveIndex);
    }


    function clearMoveHistory() {
        const movesTbody = document.getElementById('moves-tbody');
        if (movesTbody) {
            movesTbody.innerHTML = ''; // This clears all rows
        }

        moveHistory = ['start'];
        currentMoveIndex = 1;
    }


    function updateNavButtons() {
        prevBtn.disabled = currentMoveIndex <= 0;
        nextBtn.disabled = currentMoveIndex >= moveHistory.length - 1;
    }


    function handleGameResult(result, reason, whiteScore, blackScore) {
        if(reason === 'Timeout') {
            if(whiteTime < blackTime) {
                whiteTime = 0;
            }
            else {
                blackTime = 0;
            }
            updateTimersDisplay();
            timeStamps.push([whiteTime, blackTime]);
        }

        const modal = document.getElementById('game-over-modal');
        const title = document.getElementById('game-over-title');
        const message = document.getElementById('game-over-message');

        // Determine title and message
        let titleMessage = '';
        let resultMessage = '';

        if (result === 'd') {
            titleMessage = 'Draw';
            resultMessage = `By ${reason}`;
        } else if (result === 'w') {
            titleMessage = `${whiteName} Won`;
            resultMessage = `By ${reason}`;
            addChatMessage("System", `${whiteName} Won by ${reason}`);
        } else {
            titleMessage = `${blackName} Won`;
            resultMessage = `By ${reason}`;
            addChatMessage("System", `${whiteName} Won by ${reason}`);
        }

        // Set modal texts
        title.textContent = titleMessage;
        message.textContent = resultMessage;

        // Show modal
        modal.classList.remove('hidden');
    }

    function updateTimersFromServer(wTimeStr, bTimeStr) {
        whiteTime = parseInt(wTimeStr, 10);
        blackTime = parseInt(bTimeStr, 10);
        updateTimersDisplay();
    }

    function formatTime(seconds) {
        const mins = Math.floor(seconds / 60);
        const secs = seconds % 60;
        return `${mins}:${secs < 10 ? '0' : ''}${secs}`;
    }

    function updateTimersDisplay() {
        const bottomTimer = document.getElementById('bottom-timer');
        const topTimer = document.getElementById('top-timer');

        if(isBottomWhite) {
            bottomTimer.textContent = formatTime(whiteTime);
            topTimer.textContent = formatTime(blackTime);
        }
        else {
            bottomTimer.textContent = formatTime(blackTime);
            topTimer.textContent = formatTime(whiteTime);
        }
    }

    function addChatMessage(sender, message) {
        const chatMessages = document.getElementById('chat-messages');
        const messageEl = document.createElement('div');
        messageEl.innerHTML = `<span class="font-bold">${sender}:</span> ${message}`;
        chatMessages.appendChild(messageEl);
        chatMessages.scrollTop = chatMessages.scrollHeight;
    }

    // --- Timer Logic ---
    function stopTimers() {
        clearInterval(whiteTimerInterval);
        clearInterval(blackTimerInterval);
    }

    function startWhiteTimer() {
        stopTimers();
        whiteTimerInterval = setInterval(() => {
            if (whiteTime > 0) whiteTime--;
            if(currentMoveIndex == moveHistory.length - 1) updateTimersDisplay();
            if (whiteTime <= 0) stopTimers();
        }, 1000);
    }

    function startBlackTimer() {
        stopTimers();
        blackTimerInterval = setInterval(() => {
            if (blackTime > 0) blackTime--;
            if(currentMoveIndex == moveHistory.length - 1) updateTimersDisplay();
            if (blackTime <= 0) stopTimers();
        }, 1000);
    }

    function handleTurnTimer() {
        if (isGameOver) {
            stopTimers();
            return;
        }
        if (currentTurn === 'w') {
            startWhiteTimer();
        } else {
            startBlackTimer();
        }
    }

    function updateTimersAt(index) {
        const ts = timeStamps[index] || [600, 600];

        if(isBottomWhite) {
            document.getElementById('bottom-timer').innerText = formatTime(ts[0]);
            document.getElementById('top-timer').innerText = formatTime(ts[1]);
        }
        else {
            document.getElementById('bottom-timer').innerText = formatTime(ts[1]);
            document.getElementById('top-timer').innerText = formatTime(ts[0]);
        }
    }

    function changeOrientation() {
        let bottomNameEle = document.getElementById('bottom-name')
        let topNameEle = document.getElementById('top-name');
        
        const bottomName = bottomNameEle.innerText;

        bottomNameEle.innerText = topNameEle.innerText;
        topNameEle.innerText = bottomName;

        isBottomWhite = !isBottomWhite;

        updateTimersDisplay();

        board.orientation(isBottomWhite ? 'white' : 'black');
    }

    function clearHighlightedSquares() {
        lastHighlighted.forEach(square => {
            const el = document.querySelector(`.square-${square}`);
            if (el) el.classList.remove('highlight-from', 'highlight-to');
        });
        lastHighlighted = [];
    }

    function highlightMove(from, to) {
        clearHighlightedSquares();
        console.log(from, to);
        const fromEl = document.querySelector(`.square-${from}`);
        const toEl = document.querySelector(`.square-${to}`);
        if (fromEl) fromEl.classList.add('highlight-from');
        if (toEl) toEl.classList.add('highlight-to');
        lastHighlighted = [from, to];
    }


    function highlightMoveNotation(moveIndex) {
        // Clear previous
        if (lastHighlightedMoveCell) {
            lastHighlightedMoveCell.classList.remove('highlight-move');
        }

        // Find the cell with the current move index
        const moveCell = movesTbody.querySelector(`[data-move-index="${moveIndex}"]`);
        if (moveCell) {
            moveCell.classList.add('highlight-move');
            lastHighlightedMoveCell = moveCell;
        }
    }


    function insertOverlay() {
        const overlayHTML = `
            <div id="overlay"> You are being matched with an opponent... </div>
        `;

        document.body.insertAdjacentHTML('beforeend', overlayHTML);
    }

    async function getUserStatus() {
        try {
            const response = await fetch('/api/user/status', {
                method: 'GET',
                credentials: 'include'
            });

            if(!response.ok) {
                throw new Error(`HTTP error! User Status: ' ${response.status}`);
            }

            const data = await response.json();

            if(data.loggedIn === true) {
                myName = data.username;

                console.log(`Username: ${myName}`);

                const authArea = document.getElementById('auth-area');
                    authArea.innerHTML = `
                    <button id="burger" class="text-white focus:outline-none">
                        <svg class="w-6 h-6" fill="none" stroke="currentColor" stroke-width="2" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" d="M4 6h16M4 12h16M4 18h16"></path></svg>
                    </button>
                    <div id="dropdown" class="hidden absolute right-0 mt-2 w-40 bg-gray-800 rounded shadow-md z-50">
                        <a href="/player/${myName}" class="block px-4 py-2 hover:bg-gray-700">Profile</a>
                        <a id="logout-btn" href="/logout" class="block px-4 py-2 hover:bg-gray-700">Logout</a>
                    </div>
                `;

                document.getElementById('burger').onclick = () => {
                    document.getElementById('dropdown').classList.toggle('hidden');
                };

                document.getElementById('logout-btn').onclick = async (e) => {
                    e.preventDefault();
                    const res = await fetch('/logout');
                    if (res.ok) {
                        window.location.href = "/";
                    }
                };
            }  
            else {
                console.log("User is not logged in");
                chatInput.disabled = true;
                sendChatBtn.disabled = true;
                chatInput.placeholder = "Please log in to participate in chat";

                authArea.innerHTML = `
                    <button onclick="showLoginModal()" class="bg-blue-700 px-4 py-2 rounded hover:bg-blue-600">Login</button>
                    <button onclick="showSignupModal()" class="bg-green-700 px-4 py-2 rounded hover:bg-green-600">Signup</button>
                `;
            }  
        } catch (error) {
            console.error("User Status Error: ", error);
        }
    }

    function showOverlay(message) {
        const overlay = document.getElementById("overlay");
        if(message.length > 0) overlay.textContent = message;
        overlay.style.display = "flex";
    }

    function hideOverlay() {
        const overlay = document.getElementById("overlay");
        overlay.style.display = "none";
    }


    // --- Event Listeners ---
    function setupEventListeners() {
        // Move Navigation
        prevBtn.addEventListener('click', () => {
            if (currentMoveIndex > 0) {
                currentMoveIndex--;
                board.position(moveHistory[currentMoveIndex], false); // No animation
                updateTimersAt(currentMoveIndex);
                updateNavButtons();
                if(currentMoveIndex === 0) return;
                const move = document.querySelector(`[data-move-index="${currentMoveIndex}"]`).innerText;
                highlightMove(move.substring(0,2), move.substring(2, 4));
                highlightMoveNotation(currentMoveIndex);
            }
        });

        nextBtn.addEventListener('click', () => {
            if (currentMoveIndex < moveHistory.length - 1) {
                currentMoveIndex++;
                board.position(moveHistory[currentMoveIndex], false); // No animation
                updateTimersAt(currentMoveIndex);
                updateNavButtons();
                const move = document.querySelector(`[data-move-index="${currentMoveIndex}"]`).innerText;
                highlightMove(move.substring(0,2), move.substring(2, 4));
                highlightMoveNotation(currentMoveIndex);
            }
        });

        movesTbody.addEventListener('click', (e) => {
            const target = e.target.closest('[data-move-index]');
            if (target) {
                const moveIndex = parseInt(target.dataset.moveIndex, 10);
                if (!isNaN(moveIndex) && moveIndex < moveHistory.length) {
                    currentMoveIndex = moveIndex;
                    board.position(moveHistory[currentMoveIndex], false);
                    if(currentMoveIndex != moveHistory.length - 1) updateTimersAt(currentMoveIndex);
                    updateNavButtons();
                    highlightMove(target.innerText.substring(0,2), target.innerText.substring(2, 4));
                    highlightMoveNotation(currentMoveIndex);
                }
            }
        });

        document.getElementById('orientation-btn').addEventListener('click', changeOrientation);

    
        sendChatBtn.addEventListener('click', () => {
            if(!myName) return;

            const input = document.getElementById('chat-input');
            if (input.value) {
                let chatJson = {
                    type: "chat",
                    sender: myName,
                    msg: input.value
                }

                socket.send(`chat ${JSON.stringify(chatJson)}`);
                addChatMessage("You", input.value);
                input.value = '';
            }
        });
        chatInput.addEventListener('keypress', (e) => {
            if (e.key === 'Enter') document.getElementById('send-chat-btn').click();
        });
        
        document.getElementById('close-game-over-btn').addEventListener('click', () => gameOverModal.classList.add('hidden'));
    }

    function showLoginModal() {
        document.getElementById("modal-area").innerHTML = `
            <div class="fixed inset-0 flex items-center justify-center bg-black bg-opacity-70 z-50">
            <div class="bg-gray-800 p-6 rounded-lg w-full max-w-sm text-white">
                <h2 class="text-xl font-semibold mb-4">Log In</h2>
                <input id="login-username" type="text" placeholder="Username" class="w-full mb-3 px-3 py-2 bg-gray-700 rounded">
                <input id="login-password" type="password" placeholder="Password" class="w-full mb-4 px-3 py-2 bg-gray-700 rounded">
                <button onclick="handleLogin()" class="w-full bg-blue-600 hover:bg-blue-500 py-2 rounded">Log In</button>
                <button onclick="closeModal()" class="mt-2 text-sm text-gray-400 hover:underline">Cancel</button>
            </div>
            </div>`;
    }

    function showSignupModal() {
        document.getElementById("modal-area").innerHTML = `
            <div class="fixed inset-0 flex items-center justify-center bg-black bg-opacity-70 z-50">
            <div id="signup-step1" class="bg-gray-800 p-6 rounded-lg w-full max-w-sm text-white">
                <h2 class="text-xl font-semibold mb-4">Sign Up - Step 1</h2>
                <input id="signup-username" type="text" placeholder="Choose a username" class="w-full mb-2 px-3 py-2 bg-gray-700 rounded">
                <p id="username-error" class="text-red-400 text-sm mb-3 hidden">Username already exists. Try a different one.</p>
                <button onclick="checkUsername()" class="w-full bg-green-600 hover:bg-green-500 py-2 rounded">Next</button>
                <button onclick="closeModal()" class="mt-2 text-sm text-gray-400 hover:underline">Cancel</button>
            </div>
            </div>`;
    }

    function checkUsername() {
        const username = document.getElementById("signup-username").value;
        fetch(`/username/unique?username=${encodeURIComponent(username)}`)
            .then(res => res.json())
            .then(isUnique => {
                if (isUnique) {
                    document.getElementById("signup-step1").innerHTML = `
                    <h2 class="text-xl font-semibold mb-4">Sign Up - Step 2</h2>
                    <input id="signup-password" type="password" placeholder="Choose a password" class="w-full mb-4 px-3 py-2 bg-gray-700 rounded">
                    <button onclick="handleSignup('${username}')" class="w-full bg-green-600 hover:bg-green-500 py-2 rounded">Sign Up</button>
                    <button onclick="closeModal()" class="mt-2 text-sm text-gray-400 hover:underline">Cancel</button>
                    `;
                } else {
                    document.getElementById("username-error").classList.remove("hidden");
                }
            });
    }

    function handleLogin() {
        const username = document.getElementById("login-username").value;
        const password = document.getElementById("login-password").value;
        fetch("/login", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: `username=${encodeURIComponent(username)}&password=${encodeURIComponent(password)}`
        }).then(()=> {
            location.reload();
        });
    }

    function handleSignup(username) {
        const password = document.getElementById("signup-password").value;
        fetch("/signup", {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: `username=${encodeURIComponent(username)}&password=${encodeURIComponent(password)}`
        }).then(() => window.location.reload());
    }

    function closeModal() {
        document.getElementById("modal-area").innerHTML = "";
    }

    // --- Initialization ---
    async function init() {
        const config = {
            draggable: true,
            position: 'start',
            onDragStart: onDragStart,
            onDrop: onDrop,
            pieceTheme: '/img/chesspieces/wikipedia/{piece}.png',
            dropOffBoard: 'snapback',
            moveSpeed: 0,
            snapbackSpeed: 0,
            snapSpeed: 0,
            appearSpeed: 0,
            highlight: false,
            onError: "console"
        };
        board = Chessboard('board', config);   

        initWebSocket();
        setupEventListeners();
        updateNavButtons();
        addChatMessage("System", "Welcome!");
    }

    // --- Mounting Locations ---
    const rightPanel = document.querySelector('.bg-white.p-4.rounded-lg.shadow-lg.flex.flex-col');
    // insertChat(rightPanel, true);

    // Start the application
    init();
    getUserStatus();

});
