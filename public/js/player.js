// document.addEventListener("DOMContentLoaded", function () {
    
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
    let promotionMove = null;
    let sentTime = null;
    let RTT = null;
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

    // --- DOM Elements ---
    const movesTbody = document.getElementById('moves-tbody');
    const prevBtn = document.getElementById('prev-btn');
    const nextBtn = document.getElementById('next-btn');
    let drawOfferModal = document.getElementById('draw-offer-modal');
    let rematchOfferModal = document.getElementById('rematch-offer-modal');
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
                document.getElementById("top-name").innerText = opponentName;

                whiteTime = blackTime = parseInt(parts[4], 10);
                timeStamps = [[whiteTime, blackTime]];

                currentTurn = 'w'; // Game always starts with white's turn

                if(isGameOver) {
                    replaceRematchOptionsWithDrawOptions();
                    clearMoveHistory();
                    board.position('start', false);
                    isGameOver = false;
                }

                if(playerColor === 'b') isBottomWhite = false;

                board.orientation(playerColor === 'w' ? 'white' : 'black');
                updateTimersDisplay();
                startWhiteTimer();

                hideOverlay();

                addChatMessage("System", `${myName} vs ${opponentName}`);
                addChatMessage("System", `Game started. You are ${playerColor === 'w' ? 'White' : 'Black'}.`);
                break;

            case 'reconnection':
                gameID = parts[1];

                // Change URL from /game to /game/:id without reload
                history.replaceState({ gameID }, '', `/game/${gameID}`);

                playerColor = parts[2];
                opponentName = parts[3];
                document.getElementById('top-name').innerText = opponentName;

                whiteTime = parseInt(parts[4], 10);
                blackTime = parseInt(parts[5], 10);
                currentTurn = parts[6];

                let isMove = true;

                let i = 7;

                while(i < parts.length) {
                    if(parts[i].length == 0) {
                        i++;
                    }

                    if(parts[i] == "time") {
                        i++;
                        break;
                    }

                    makeMove(parts[i], false);
                    updateMoveHistory(parts[i], i%2 ? "w" : "b", board.fen());

                    i++;
                }

                while(i < parts.length) {
                    timeStamps.push([parseInt(parts[i].substring(0, parts[i].indexOf('-') + 1)), parseInt(parts[i].substring(parts[i].indexOf('-')+1, parts[i].length))]);
                    i++;
                }

                board.orientation(playerColor === 'w' ? 'white' : 'black');

                updateTimersDisplay();
                handleTurnTimer();

                hideOverlay();

                addChatMessage("System", `${myName} vs ${opponentName}`);
                break;

            case 'start-rematch':
                gameID = parts[1];
                // Change URL from /game to /game/:id without reload
                history.replaceState({ gameID }, '', `/game/${gameID}`);
                currentTurn = "w";
                playerColor = parts[2];
                whiteTime = parseInt(parts[3], 10);
                blackTime = parseInt(parts[4], 10);
                timeStamps = [[whiteTime, blackTime]];

                replaceRematchOptionsWithDrawOptions();
                clearMoveHistory();
                board.position('start', false);

                board.orientation(playerColor === 'w' ? 'white' : 'black');

                updateTimersDisplay();
                startWhiteTimer();
                isGameOver = false;

                addChatMessage("System", `${myName} vs ${opponentName}`);
                break;

            case 'move': // Response to our move attempt
                if (parts[1] === 'false') {
                    board.position(moveHistory[moveHistory.length - 1], false);
                    addChatMessage("System", "Invalid move rejected by server.");
                    break;
                }
                else {
                    // sync timers with server
                    let receivedTime = performance.now();

                    RTT = receivedTime - sentTime;

                    let wsec = parseInt(parts[2], 10);
                    let wnsec = parseInt(parts[3], 10);
                    let bsec = parseInt(parts[4], 10);
                    let bnsec = parseInt(parts[5], 10);

                    if(currentTurn == 'w') {
                        let wTimeMs = wsec * 1000 + wnsec / 1e6;
                        let bTimeMs = bsec * 1000 + bnsec / 1e6;

                        wTimeMs -= RTT;
                        
                        wTimeMs = Math.ceil(wTimeMs/1000);
                        bTimeMs = Math.ceil(bTimeMs/1000);

                        updateTimersFromServer(wTimeMs, bTimeMs);
                        timeStamps.push([wTimeMs, bTimeMs]);
                    }
                    else {
                        let wTimeMs = wsec * 1000 + wnsec / 1e6;
                        let bTimeMs = bsec * 1000 + bnsec / 1e6;

                        bTimeMs -= RTT;

                        wTimeMs = Math.ceil(wTimeMs/1000);
                        bTimeMs = Math.ceil(bTimeMs/1000)

                        updateTimersFromServer(wTimeMs, bTimeMs);
                        timeStamps.push([wTimeMs, bTimeMs]);
                    }

                    makeMove(lastMove, true);
                    updateMoveHistory(lastMove, currentTurn, board.fen());

                    changeTurn();
                    handleTurnTimer();                    
                }
                break;

            case 'newmove': // A move was made (opponent)
                if(currentMoveIndex != moveHistory.length - 1) {
                    board.position(moveHistory[moveHistory.length - 1], false);
                    currentMoveIndex = moveHistory.length - 1;
                }

                let wsec = parseInt(parts[2], 10);
                let wnsec = parseInt(parts[3], 10);
                let bsec = parseInt(parts[4], 10);
                let bnsec = parseInt(parts[5], 10);

                let wTimeMs = wsec * 1000 + wnsec / 1e6;
                let bTimeMs = bsec * 1000 + bnsec / 1e6;

                wTimeMs = Math.ceil(wTimeMs/1000);
                bTimeMs = Math.ceil(bTimeMs/1000);

                updateTimersFromServer(wTimeMs, bTimeMs);
                timeStamps.push([wTimeMs, bTimeMs]);

                makeMove(parts[1], false);
                updateMoveHistory(parts[1], currentTurn, board.fen());

                changeTurn();
                handleTurnTimer();
                break;
            
            case 'result': // Game has ended
                isGameOver = true;
                stopTimers();
                handleGameResult(parts[1], parts[2], parts[3], parts[4]);
                break;

            case 'draw':
                if(!drawOfferModal) drawOfferModal = document.getElementById('draw-offer-modal');
                drawOfferModal.classList.remove('hidden');
                break;
            case 'rematch':
                if(!rematchOfferModal) rematchOfferModal = document.getElementById('rematch-offer-modal');
                rematchOfferModal.classList.remove('hidden');
                break;
            case 'chat':
                const chatMsg = parts.slice(1).join(' ');
                addChatMessage(opponentName, chatMsg);
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
        if(isGameOver) return;
        
        const position = board.position();
        const piece = position[source];
        const isPawn = piece && piece[1].toLowerCase() === 'p';

        const isWhite = piece && piece[0] === 'w';
        const direction = isWhite ? 1 : -1;

        const isPromotionRank = (playerColor === 'w' && source[1] === '7' && target[1] === '8') || (playerColor === 'b' && source[1] == '2' && target[1] === '1');

        if(isPawn && isPromotionRank) {
            promotionMove = {source, target};
            showPromotionDialog(currentTurn);  
            return;
        }

        // We no longer validate the move here. Just send it to the server.
        console.log(`Sending move: move ${source}${target}`);
        sentTime = performance.now();
        socket.send(`move ${source}${target}`);
        lastMove = `${source}${target}`;
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
        if (piece.charAt(1) === 'P' && Math.abs(parseInt(source[1]) - parseInt(target[1])) === 2) {
            const middleRank = (parseInt(source[1]) + parseInt(target[1])) / 2;
            enpassantSquare = target[0] + middleRank;
        }
        else enpassantSquare = null;
    }

    function showPromotionDialog(color) {
        console.log("Show promotion dialogue");
        if(color == "w")
            $('#promotionDialogWhite').removeClass('hidden');
        else 
            $('#promotionDialogBlack').removeClass('hidden');
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

    
    function clearMoveHistory() {
        const movesTbody = document.getElementById('moves-tbody');
        if (movesTbody) {
            movesTbody.innerHTML = ''; // This clears all rows
        }

        moveHistory = ['start'];
        currentMoveIndex = 0;
    }


    function updateNavButtons() {
        prevBtn.disabled = currentMoveIndex <= 0;
        nextBtn.disabled = currentMoveIndex >= moveHistory.length - 1;
    }

    function replaceDrawOptionsWithRematchOptions() {
        const actionsContainer = document.getElementById('game-actions');
        if (!actionsContainer) return;

        // Clear existing buttons from the container
        actionsContainer.innerHTML = '';

        /**
         * Creates a button element with styles matching the original HTML.
         * @param {string} id - The ID for the button.
         * @param {string} text - The text content of the button.
         * @param {string} bgColor - The Tailwind CSS class for the background color.
         * @param {string} hoverColor - The Tailwind CSS class for the hover state background color.
         * @param {function} handler - The function to execute on click.
         * @returns {HTMLButtonElement}
         */
        const createButton = (id, text, bgColor, hoverColor, handler) => {
            const btn = document.createElement('button');
            btn.id = id;
            btn.textContent = text;
            // Apply the exact same classes as the original HTML buttons
            btn.className = `w-full px-4 py-2 ${bgColor} text-white rounded-lg ${hoverColor}`;
            btn.onclick = handler;
            return btn;
        };

        const rematchBtn = createButton(
            'rematch-btn',
            'Rematch',
            'bg-blue-600',
            'hover:bg-blue-700',
            () => {
                if (socket) {
                    socket.send('req-rematch');
                } else {
                    const para = document.createElement('p');
                    para.textContent = 'Connection lost. Please start a new game.';
                    para.className = 'col-span-2 text-center text-sm';
                    actionsContainer.appendChild(para);
                }
            }
        );

        const newGameBtn = createButton(
            'new-game-btn',
            'New Game',
            'bg-green-600',
            'hover:bg-green-700',
            () => {
                if (socket) {
                    socket.send('new-game');
                } else {
                    initWebSocket();
                }
            }
        );

        // Append new buttons directly to the grid container
        actionsContainer.appendChild(rematchBtn);
        actionsContainer.appendChild(newGameBtn);
    }


    function replaceRematchOptionsWithDrawOptions() {
        const actionsContainer = document.getElementById('game-actions');
        if (!actionsContainer) return;

        // Clear existing buttons from the container
        actionsContainer.innerHTML = '';

        /**
         * Creates a button element with styles matching the original HTML.
         * @param {string} id - The ID for the button.
         * @param {string} text - The text content of the button.
         * @param {string} bgColor - The Tailwind CSS class for the background color.
         * @param {string} hoverColor - The Tailwind CSS class for the hover state background color.
         * @param {function} handler - The function to execute on click.
         * @returns {HTMLButtonElement}
         */
        const createButton = (id, text, bgColor, hoverColor, handler) => {
            const btn = document.createElement('button');
            btn.id = id;
            btn.textContent = text;
            // Apply the exact same classes as the original HTML buttons
            btn.className = `w-full px-4 py-2 ${bgColor} text-white rounded-lg ${hoverColor}`;
            btn.onclick = handler;
            return btn;
        };

        const drawBtn = createButton(
            'draw-btn',
            'Offer Draw',
            'bg-blue-600',
            'hover:bg-blue-700',
            () => {
                if (socket) {
                    socket.send('req-draw');
                }
            }
        );

        const resignBtn = createButton(
            'resign-btn',
            'Resign',
            'bg-red-600',
            'hover:bg-red-700',
            () => {
                if (socket) {
                    socket.send('resign');
                }
            }
        );

        // Append new buttons directly to the grid container
        actionsContainer.appendChild(drawBtn);
        actionsContainer.appendChild(resignBtn);
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
        replaceDrawOptionsWithRematchOptions();
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
                document.getElementById("bottom-name").innerText = data.username;
                myName = data.username;

                console.log(`Username: ${myName}`);
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


    // --- Event Listeners ---
    function setupEventListeners() {
        // Move Navigation
        prevBtn.addEventListener('click', () => {
            if (currentMoveIndex > 0) {
                currentMoveIndex--;
                board.position(moveHistory[currentMoveIndex], false); // No animation
                updateTimersAt(currentMoveIndex);
                updateNavButtons();
            }
        });

        nextBtn.addEventListener('click', () => {
            if (currentMoveIndex < moveHistory.length - 1) {
                currentMoveIndex++;
                board.position(moveHistory[currentMoveIndex], false); // No animation
                updateTimersAt(currentMoveIndex);
                updateNavButtons();
            }
        });

        movesTbody.addEventListener('click', (e) => {
            const target = e.target.closest('[data-move-index]');
            if (target) {
                const moveIndex = parseInt(target.dataset.moveIndex, 10);
                if (!isNaN(moveIndex) && moveIndex < moveHistory.length) {
                    currentMoveIndex = moveIndex;
                    console.log(currentMoveIndex);
                    board.position(moveHistory[currentMoveIndex], false);
                    if(currentMoveIndex != moveHistory.length - 1) updateTimersAt(currentMoveIndex);
                    updateNavButtons();
                }
            }
        });

        document.getElementById('orientation-btn').addEventListener('click', changeOrientation);

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
            let {source, target} = promotionMove;

            lastMove = `${source}${target}${piece}`;
        
            console.log(`Sending move: move ${lastMove}`);
            socket.send(`move ${lastMove}`);
            
            if(currentTurn == "w")
                $('#promotionDialogWhite').addClass('hidden');
            else 
                $('#promotionDialogBlack').addClass('hidden');

            promotionMove = null;
        });
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

        await getUserStatus();
        initWebSocket();
        setupEventListeners();
        updateNavButtons();
        addChatMessage("System", "Welcome! Waiting for game to start...");
    }

    // --- Mounting Locations ---
    // const rightPanel = document.querySelector('.bg-white.p-4.rounded-lg.shadow-lg.flex.flex-col');
    // if (rightPanel) {
    //     insertGameActions(rightPanel);
    //     insertChat(rightPanel, /* set true for live chat */ false);
    // }

    // Draw and Rematch Modals
    // insertModals();

    // insertPromotionDialog();

    // insertOverlay();

    // Start the application
    init();

// });
