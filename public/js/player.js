
    let board = null;
    let socket = null;

    // Game state is now managed manually, based on server messages
    let playerColor = 'w';
    let currentTurn = 'w';
    let isGameOver = false;
    let gameID = "";
    let myName = "";
    let opponentName = "";
    let lastMove = "";
    let promotionSquare = null;

    // History for move navigation, stores FEN strings
    let moveHistory = ['rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1']; // Start with initial FEN
    let currentMoveIndex = 0; // Index for the moveHistory array

    // Timers
    let whiteTimerInterval, blackTimerInterval;
    let whiteTime = 0;
    let blackTime = 0;

    // --- DOM Elements ---
    const movesTbody = document.getElementById('moves-tbody');
    const prevBtn = document.getElementById('prev-btn');
    const nextBtn = document.getElementById('next-btn');
    const drawOfferModal = document.getElementById('draw-offer-modal');
    const rematchOfferModal = document.getElementById('rematch-offer-modal');
    const gameOverModal = document.getElementById('game-over-modal');

    // --- WebSocket Logic ---
    function initWebSocket() {
        socket = new WebSocket("ws://192.168.155.94:8080/new-game");

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
                board.draggable = false;
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
        const parts = message.split(' ');
        const command = parts[0];

        switch (command) {
            case 'start-game':
                gameID = parts[1];

                // Change URL from /game to /game/:id without reload
                history.replaceState({ gameID }, '', `/game/${gameID}`);

                playerColor = parts[2];
                opponentName = parts[3];
                document.getElementById("opponent-name").innerText = opponentName;

                whiteTime = blackTime = parseInt(parts[4], 10);

                currentTurn = 'w'; // Game always starts with white's turn

                if(isGameOver) {
                    resetGameActions(rightPanel);
                }

                isGameOver = false;

                moveHistory = ['start'];

                hideOverlay();
                board.draggable = true;

                board.orientation(playerColor === 'w' ? 'white' : 'black');
                updateTimersDisplay();
                startWhiteTimer();
                addChatMessage("System", `Game started. You are ${playerColor === 'w' ? 'White' : 'Black'}.`);
                break;

            case 'reconnection':
                gameID = parts[1];

                // Change URL from /game to /game/:id without reload
                history.replaceState({ gameID }, '', `/game/${gameID}`);

                playerColor = parts[2];
                opponentName = parts[3];
                whiteTime = parseInt(parts[4], 10);
                blackTime = parseInt(parts[5], 10);
                currentTurn = parts[6];

                for(let i = 7; i<parts.length; i++) {
                    board.move(`${parts[i].substring(0, 2)}-${parts[i].substring(2, 4)}`);
                    updateMoveHistory(parts[i], i%2 ? "w" : "b", board.fen());
                }

                hideOverlay();
                board.draggable = true;

                board.orientation(playerColor === 'w' ? 'white' : 'black');

                if(playerColor === currentTurn) {
                    if(playerColor === "w") startWhiteTimer();
                    else startBlackTimer();
                }

            case 'move': // Response to our move attempt
                if (parts[1] === 'false') {
                    board.position(moveHistory[moveHistory.length - 1], false);
                    addChatMessage("System", "Invalid move rejected by server.");
                    break;
                }
                else {
                    if(isCastle(lastMove, true)) {
                        board.position(board.position(), false);
                    }
                    else if(lastMove.length == 5) {
                        let position = board.position();

                        const newPiece = lastMove[4];
                        let {from, to} = promotionSquare;

                        position[to] = newPiece;
                        delete position[from];

                        board.position(position, false);
                    }
                    else {
                        board.move(`${lastMove.substring(0, 2)}-${lastMove.substring(2, 4)}`);
                        board.position(board.position(), false);
                    }

                    updateMoveHistory(lastMove, currentTurn, board.fen());
                    changeTurn();
                    handleTurnTimer();
                }
                break;

            case 'newmove': // A move was made (opponent)
                // Example: newmove e2e4 
                if(currentMoveIndex != moveHistory.length - 1) {
                    board.position(moveHistory[moveHistory.length - 1], false);
                    currentMoveIndex = moveHistory.length - 1;
                }

                const uciMove = parts[1];

                if(isCastle(uciMove, false)) {
                    board.position(board.position(), false);
                }
                else if(uciMove.length == 5) {
                    let position = board.position();
                    const newPiece = uciMove[4];

                    let from = uciMove.substring(0, 2);
                    let to = uciMove.substring(2, 4);

                    position[to] = newPiece;
                    delete position[from];

                    board.position(position, false);
                }
                else {
                    board.move(`${uciMove.substring(0, 2)}-${uciMove.substring(2, 4)}`);
                    board.position(board.position(), false);
                }

                const newFen = board.fen();
                updateMoveHistory(uciMove, currentTurn, newFen);
                changeTurn();
                handleTurnTimer();
                break;
            
            case 'result': // Game has ended
                isGameOver = true;
                stopTimers();
                handleGameResult(parts[1], parts[2], parts[3], parts[4]);
                break;

            // Other cases remain the same
            case 'draw':
                drawOfferModal.classList.remove('hidden');
                break;
            case 'rematch':
                rematchOfferModal.classList.remove('hidden');
                break;
            case 'chat':
                const sender = parts[1];
                const chatMsg = parts.slice(2).join(' ');
                addChatMessage(sender, chatMsg);
                break;
            case 'time':
                updateTimersFromServer(parts[1], parts[2]);
                break;
            case 'dr':
                addChatMessage("System", "Your draw offer was rejected.");
                break;
            case 'rr':
                addChatMessage("System", "Your rematch offer was rejected.");
                break;
            case 'rn':
                addChatMessage("System", "Opponent is not available for rematch.");
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
        // Do not pick up pieces if the game is over
        if (isGameOver) return false;

        // Only pick up pieces for the side to move
        if (currentTurn !== playerColor) return false;

        // Check piece color against whose turn it is
        const pieceColor = piece.charAt(0);
        if (pieceColor !== currentTurn) return false;

        // Only allow moves from the live board position
        if (currentMoveIndex !== moveHistory.length - 1) {
            addChatMessage("System", "Go to the latest move to play.");
            return false;
        }
    }

    function onDrop(source, target) {
        if(source === target || isGameOver) return;
        
        const position = board.position();
        const piece = position[source];
        const isPawn = piece && piece[1].toLowerCase() === 'p';

        const isWhite = piece && piece[0] === 'w';
        const direction = isWhite ? 1 : -1;

        const isPromotionRank = (playerColor === 'w' && target[1] === '8') || (playerColor === 'b' && target[1] === '1');

        if(isPawn && isPromotionRank) {
            promotionSquare = {source, target};
            showPromotionDialog();  
            return;
        }

        // We no longer validate the move here. Just send it to the server.
        console.log(`Sending move: move ${source}${target}`);
        socket.send(`move ${source}${target}`);
        lastMove = `${source}${target}`;
    }

    function showPromotionDialog(source, target) {
        console.log("Show promotion dialogue");
        pendingPromotion = { source, target };
        $('#promotionDialog').removeClass('hidden flex').addClass('flex');
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
    }

    function updateNavButtons() {
        prevBtn.disabled = currentMoveIndex <= 0;
        nextBtn.disabled = currentMoveIndex >= moveHistory.length - 1;
    }

    function replaceGameActionsWithRematchOptions() {
        const actionsContainer = document.querySelector('.bg-white .grid.grid-cols-2');
        if (!actionsContainer) return;

        actionsContainer.innerHTML = ''; // Clear existing buttons

        const createButton = (text, bgColor, hoverColor, handler) => {
            const btn = document.createElement('button');
            btn.textContent = text;
            btn.className = `w-full px-4 py-2 ${bgColor} text-white rounded-lg hover:${hoverColor} transition duration-200`;
            btn.onclick = handler;
            return btn;
        };

        const rematchBtn = createButton(
            'Rematch',
            'bg-blue-600',
            'bg-blue-700',
            () => {
                if(socket) {
                    socket.send('req-rematch');
                }
                else {
                    const para = `<p> Connection lost. Please start a new game <\p>`
                    document.getElementById('game-actions').appendChild(para);
                }
            }
        );

        const newGameBtn = createButton(
            'New Game',
            'bg-green-600',
            'bg-green-700',
            () => {
                if(socket) {
                    socket.send('new-game');
                }
                else {
                    initWebSocket();
                }
            }
        );
    }


    function handleGameResult(result, reason, whiteScore, blackScore) {
        const modal = document.getElementById('game-over-modal');
        const title = document.getElementById('game-over-title');
        const message = document.getElementById('game-over-message');

        // Determine title and message
        let titleMessage = '';
        let resultMessage = '';

        if (result === 'd') {
            titleMessage = 'Draw';
            resultMessage = `By ${reason}`;
        } else if ((result === 'w' && playerColor === 'w') || (result === 'b' && playerColor === 'b')) {
            titleMessage = 'You Won';
            resultMessage = `By ${reason}`;
        } else {
            titleMessage = 'You Lost';
            resultMessage = `By ${reason}`;
        }

        // Set modal texts
        title.textContent = titleMessage;
        message.textContent = resultMessage;

        // Show modal
        modal.classList.remove('hidden');

        // Also update the right panel actions
        replaceGameActionsWithRematchOptions();
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
        const playerTimerEl = document.getElementById('player-timer');
        const opponentTimerEl = document.getElementById('opponent-timer');

        playerTimerEl.textContent = formatTime(playerColor === 'w' ? whiteTime : blackTime);
        opponentTimerEl.textContent = formatTime(playerColor === 'b' ? whiteTime : blackTime);
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
            updateTimersDisplay();
            if (whiteTime <= 0) stopTimers();
        }, 1000);
    }

    function startBlackTimer() {
        stopTimers();
        blackTimerInterval = setInterval(() => {
            if (blackTime > 0) blackTime--;
            updateTimersDisplay();
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

    // Insert Game Actions dynamically
    function insertGameActions(container) {
        const actionsHTML = `
            <div id="game-actions">
                <h3 class="text-2xl font-bold mb-3 text-center">Actions</h3>
                <div class="grid grid-cols-2 gap-3">
                    <button id="draw-btn" class="w-full px-4 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 transition duration-200">Offer Draw</button>
                    <button id="resign-btn" class="w-full px-4 py-2 bg-red-600 text-white rounded-lg hover:bg-red-700 transition duration-200">Resign</button>
                </div>
            </div>
            <hr class="my-4">
        `;
        container.insertAdjacentHTML('beforeend', actionsHTML);
    }

    // Insert Chat dynamically
    function insertChat(container, spectator = false) {
        const chatTitle = spectator ? "Live Chat" : "Chat";
        const chatHTML = `
            <div id="chat-section" class="flex-grow flex flex-col">
                <h3 class="text-2xl font-bold mb-2 text-center">${chatTitle}</h3>
                <div id="chat-messages" class="chat-messages flex-grow border rounded-lg p-3 bg-gray-50 mb-3">
                    <!-- Messages go here -->
                </div>
                <div class="flex">
                    <input type="text" id="chat-input" class="flex-grow border rounded-l-lg p-2 focus:outline-none focus:ring-2 focus:ring-blue-500" placeholder="Type a message...">
                    <button id="send-chat-btn" class="bg-blue-600 text-white px-4 rounded-r-lg hover:bg-blue-700 transition duration-200">Send</button>
                </div>
            </div>
        `;
        container.insertAdjacentHTML('beforeend', chatHTML);
    }

    // Insert modals dynamically
    function insertModals() {
        const modalsHTML = `
            <!-- Draw Offer Modal -->
            <div id="draw-offer-modal" class="modal-backdrop hidden">
                <div class="modal-content">
                    <h3 class="text-xl font-bold mb-4">Draw Offer Received</h3>
                    <p class="mb-6">Your opponent has offered a draw.</p>
                    <div class="flex justify-center space-x-4">
                        <button id="accept-draw-btn" class="px-6 py-2 bg-green-600 text-white rounded-lg hover:bg-green-700">Accept</button>
                        <button id="reject-draw-btn" class="px-6 py-2 bg-red-600 text-white rounded-lg hover:bg-red-700">Reject</button>
                    </div>
                </div>
            </div>

            <!-- Rematch Offer Modal -->
            <div id="rematch-offer-modal" class="modal-backdrop hidden">
                <div class="modal-content">
                    <h3 class="text-xl font-bold mb-4">Rematch Offer</h3>
                    <p class="mb-6">Your opponent would like a rematch.</p>
                    <div class="flex justify-center space-x-4">
                        <button id="accept-rematch-btn" class="px-6 py-2 bg-green-600 text-white rounded-lg hover:bg-green-700">Accept</button>
                        <button id="reject-rematch-btn" class="px-6 py-2 bg-red-600 text-white rounded-lg hover:bg-red-700">Reject</button>
                    </div>
                </div>
            </div>
        `;
        document.body.insertAdjacentHTML('beforeend', modalsHTML);
    }

    function insertPromotionDialog() {
        const promotionDialogHTML = `
            <div id="promotionDialog" class="fixed inset-0 bg-black bg-opacity-70 hidden items-center justify-center z-50">
                <div class="bg-gray-800 p-6 rounded-xl text-white flex gap-4">
                    <button data-piece="q" class="promotion-piece">♕</button>
                    <button data-piece="r" class="promotion-piece">♖</button>
                    <button data-piece="b" class="promotion-piece">♗</button>
                    <button data-piece="n" class="promotion-piece">♘</button>
                </div>
            </div>
        `;
        document.body.insertAdjacentHTML('beforeend', promotionDialogHTML);
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
                document.getElementById("player-name").innerText = data.username;
            }  
            else {
                console.log("User is not logged in");
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

    function resetGameActions(container) {
        const existing = container.querySelector('#game-actions');
        if (existing) {
            existing.remove(); // remove old actions
        }

        // // Optionally remove any <hr> after it if added
        // const hr = container.querySelector('hr');
        // if (hr && !container.querySelector('#game-actions')) {
        //     hr.remove();
        // }

        insertGameActions(container); // re-insert original actions
    }


    // --- Event Listeners ---
    function setupEventListeners() {
        // Move Navigation
        prevBtn.addEventListener('click', () => {
            if (currentMoveIndex > 0) {
                currentMoveIndex--;
                board.position(moveHistory[currentMoveIndex], false); // No animation
                updateNavButtons();
            }
        });

        nextBtn.addEventListener('click', () => {
            if (currentMoveIndex < moveHistory.length - 1) {
                currentMoveIndex++;
                board.position(moveHistory[currentMoveIndex], false); // No animation
                updateNavButtons();
            }
        });

        movesTbody.addEventListener('click', (e) => {
            const target = e.target.closest('[data-move-index]');
            if (target) {
                const moveIndex = parseInt(target.dataset.moveIndex, 10);
                if (!isNaN(moveIndex) && moveIndex < moveHistory.length) {
                    currentMoveIndex = moveIndex;
                    board.position(moveHistory[currentMoveIndex], false);
                    updateNavButtons();
                }
            }
        });

        // Game Actions and Chat listeners remain the same...
        document.getElementById('draw-btn').addEventListener('click', () => socket.send('req-draw'));
        document.getElementById('resign-btn').addEventListener('click', () => socket.send('resign'));
        document.getElementById('send-chat-btn').addEventListener('click', () => {
            const input = document.getElementById('chat-input');
            if (input.value) {
                socket.send(`chat ${input.value}`);
                addChatMessage("You", input.value);
                input.value = '';
            }
        });
        document.getElementById('chat-input').addEventListener('keypress', (e) => {
            if (e.key === 'Enter') document.getElementById('send-chat-btn').click();
        });
        document.getElementById('accept-draw-btn').addEventListener('click', () => { socket.send('res-draw a'); drawOfferModal.classList.add('hidden'); });
        document.getElementById('reject-draw-btn').addEventListener('click', () => { socket.send('res-draw r'); drawOfferModal.classList.add('hidden'); });
        document.getElementById('accept-rematch-btn').addEventListener('click', () => { socket.send('res-rematch a'); rematchOfferModal.classList.add('hidden'); });
        document.getElementById('reject-rematch-btn').addEventListener('click', () => { socket.send('res-rematch r'); rematchOfferModal.classList.add('hidden'); });
        document.getElementById('close-game-over-btn').addEventListener('click', () => gameOverModal.classList.add('hidden'));
    
        $('.promotion-piece').on('click', function () {
            const piece = $(this).data('piece');
            let {source, target} = promotionSquare;

            lastMove = `${source}${target}${piece}`;
        
            console.log(`Sending move: move ${lastMove}`);
            socket.send(`move ${lastMove}`);
        
            $('#promotionDialog').addClass('hidden').removeClass('flex');
        });
    }

    // --- Initialization ---
    function init() {
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

        showOverlay('');
        board.draggable = false;

        getUserStatus();
        initWebSocket();
        setupEventListeners();
        updateNavButtons();
        addChatMessage("System", "Welcome! Waiting for game to start...");
    }

    // --- Mounting Locations ---
    const rightPanel = document.querySelector('.bg-white.p-4.rounded-lg.shadow-lg.flex.flex-col');
    if (rightPanel) {
        insertGameActions(rightPanel);
        insertChat(rightPanel, /* set true for live chat */ false);
    }

    // Draw and Rematch Modals
    insertModals();

    insertPromotionDialog();

    // Start the application
    init();
