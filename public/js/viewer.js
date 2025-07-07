// document.addEventListener("DOMContentLoaded", function () {

    const charToReason = {
        r: "Repitition",
        c: "Checkmate",
        t: "Timeout",
        a: "Agreement",
        f: "Fifty Half Moves",
        s: "Stalemate",
        A: "Abandonment",
        R: "Resignation"
    };

    let myName = null;
    const boardElementId = 'board';
    const movesTbody = document.getElementById('moves-tbody');
    const prevBtn = document.getElementById('prev-btn');
    const nextBtn = document.getElementById('next-btn');
    let currentTurn = 'w';
    let isBottomWhite = true;

    let board = null;
    let moveHistory = ['start']; // FEN history
    let timeStamps = [];
    let currentMoveIndex = 0;

    let enpassantSquare = null;

    let lastHighlighted = [];
    let lastHighlightedMoveCell = null;

    function updateNavButtons() {
        prevBtn.disabled = currentMoveIndex <= 0;
        nextBtn.disabled = currentMoveIndex >= moveHistory.length - 1;
    }

    function updateTimersAt(index) {
        const ts = timeStamps[index];

        if(isBottomWhite) {
            document.getElementById('bottom-timer').innerText = formatTime(ts[0]);
            document.getElementById('top-timer').innerText = formatTime(ts[1]);
        }
        else {
            document.getElementById('bottom-timer').innerText = formatTime(ts[1]);
            document.getElementById('top-timer').innerText = formatTime(ts[0]);
        }
    }

    function formatTime(seconds) {
        const m = Math.floor(seconds / 60);
        const s = seconds % 60;
        return `${m}:${s.toString().padStart(2, '0')}`;
    }

    function updateMoveHistory(san, color, fen, index) {
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

    function insertGameResultBlock(result, reason, whiteUsername, blackUsername) {
        // Select the right panel element. The class names are updated to match the HTML.
        const rightPanel = document.querySelector('.lg\\:col-span-2.bg-gray-800.p-4.rounded-lg.shadow-lg.flex.flex-col');
        if (!rightPanel) {
            console.error("Right panel not found for inserting game result.");
            return;
        }

        // Create the main div for the game result block.
        const resultDiv = document.createElement('div');
        // Apply Tailwind classes for styling, now matching the dark theme:
        // - bg-gray-700 for the background (darker than the panel, but still dark)
        // - border-l-4 border-blue-500 for a distinct blue left border
        // - rounded shadow for consistent styling
        resultDiv.className = 'mt-4 p-4 bg-gray-700 border-l-4 border-blue-500 rounded shadow';

        // Create and append the title for the game result.
        const title = document.createElement('h3');
        // text-blue-300 for a visible and thematic title color on the dark background
        title.className = 'text-xl font-bold mb-2 text-blue-300';
        title.textContent = 'Game Result';
        resultDiv.appendChild(title);

        // Create and append the paragraph for the result message.
        const resultPara = document.createElement('p');
        // text-gray-200 for the message text, ensuring readability on the dark background
        resultPara.className = 'text-gray-200 font-medium';

        // Determine the result message based on the game outcome.
        if (result === "d") {
            resultPara.textContent = `Draw by ${reason}`;
        } else if (result === "w") {
            resultPara.textContent = `${whiteUsername} won by ${reason}`;
        } else if (result === "b") {
            resultPara.textContent = `${blackUsername} won by ${reason}`;
        } else {
            resultPara.textContent = `Result unavailable`;
        }

        resultDiv.appendChild(resultPara);

        // Insert the result div into the right panel.
        // It will be inserted before the first <hr> element found in the right panel.
        const allChildren = rightPanel.children;
        let inserted = false;
        for (let i = 0; i < allChildren.length; i++) {
            if (allChildren[i].tagName === 'HR') {
                rightPanel.insertBefore(resultDiv, allChildren[i]);
                inserted = true;
                break;
            }
        }

        // Fallback: if no <hr> is found, append it to the end of the right panel.
        if (!inserted) {
            rightPanel.appendChild(resultDiv);
        }
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

    function changeOrientation() {
        let bottomNameEle = document.getElementById('bottom-name')
        let topNameEle = document.getElementById('top-name');
        
        const bottomName = bottomNameEle.innerText;

        bottomNameEle.innerText = topNameEle.innerText;
        topNameEle.innerText = bottomName;

        isBottomWhite = !isBottomWhite;

        updateTimersAt(currentMoveIndex);

        board.orientation(isBottomWhite ? 'white' : 'black');
    }

    function setupEventListeners() {
        prevBtn.addEventListener('click', () => {
            if (currentMoveIndex > 0) {
                currentMoveIndex--;
                board.position(moveHistory[currentMoveIndex], false);
                updateTimersAt(currentMoveIndex);
                updateNavButtons();
                currentTurn = currentMoveIndex % 2 ? 'b' : 'w';
                if(currentMoveIndex === 0) return;
                const move = document.querySelector(`[data-move-index="${currentMoveIndex}"]`).innerText;
                highlightMove(move.substring(0,2), move.substring(2, 4));
                highlightMoveNotation(currentMoveIndex);
            }
        });

        nextBtn.addEventListener('click', () => {
            if (currentMoveIndex < moveHistory.length - 1) {
                currentMoveIndex++;
                board.position(moveHistory[currentMoveIndex], false);
                updateTimersAt(currentMoveIndex);
                updateNavButtons();
                currentTurn = currentMoveIndex % 2 ? 'b' : 'w';
                const move = document.querySelector(`[data-move-index="${currentMoveIndex}"]`).innerText;
                highlightMove(move.substring(0,2), move.substring(2, 4));
                highlightMoveNotation(currentMoveIndex);
            }
        });

        movesTbody.addEventListener('click', (e) => {
            const target = e.target.closest('[data-move-index]');
            if (target) {
                const idx = parseInt(target.dataset.moveIndex, 10);
                if (!isNaN(idx)) {
                    currentMoveIndex = idx;
                    board.position(moveHistory[currentMoveIndex], false);
                    updateTimersAt(idx);
                    updateNavButtons();
                    currentTurn = currentMoveIndex % 2 ? 'b' : 'w';
                    highlightMove(target.innerText.substring(0,2), target.innerText.substring(2, 4));
                    highlightMoveNotation(currentMoveIndex);
                }
            }
        });

        document.getElementById('orientation-btn').addEventListener('click', changeOrientation);
    }

    function onDragStart(source, piece) {
        if(currentTurn != piece.charAt(0)) return false;

        // disable scroll
        document.body.style.overflow = 'hidden';
    }

    function onDrop(source, target) {
        // enable scroll
        document.body.style.overflow = '';
        
        const position = board.position();
        if(position[target] && position[target].charAt(0) == currentTurn) return 'snapback';

        currentTurn = currentTurn == 'w' ? 'b' : 'w';
        highlightMove(source, target);
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

    function makeMove(move, madeOnBoard) {
        console.log(move);
        let source = move.substring(0, 2);
        let target = move.substring(2, 4);

        let position = board.position();
        const piece = position[source];
        const color = piece.charAt(0);

        if(move.length == 5) {
            position[source] = null;
            position[target] =  color + move[4].toUpperCase();

            board.position(position, false);
        }
        else if(isCastle(move, madeOnBoard)) {
            board.position(board.position(), false);
        }
        else if(piece.charAt(1) === 'P' && target == enpassantSquare) {
            let rank = parseInt(target[1], 10);
            let captureSquare = target[0] + (color == 'w' ? (rank - 1) : (rank + 1));

            position[captureSquare] = null;
            position[source] = null;
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

    function showOverlay(message) {
        const overlay = document.getElementById("overlay");
        if(message.length > 0) overlay.textContent = message;
        overlay.style.display = "flex";
    }

    function hideOverlay() {
        const overlay = document.getElementById("overlay");
        overlay.style.display = "none";
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

                authArea.innerHTML = `
                    <button onclick="showLoginModal()" class="bg-blue-700 px-4 py-2 rounded hover:bg-blue-600">Login</button>
                    <button onclick="showSignupModal()" class="bg-green-700 px-4 py-2 rounded hover:bg-green-600">Signup</button>
                `;
            }  
        } catch (error) {
            console.error("User Status Error: ", error);
        }
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

    function initBoard() {
        const config = {
            draggable: true,
            position: 'start',
            pieceTheme: '/img/chesspieces/wikipedia/{piece}.png',
            onDragStart: onDragStart,
            onDrop: onDrop,
            dropOffBoard: 'snapback',
            moveSpeed: 0,
            snapbackSpeed: 0,
            snapSpeed: 0,
            appearSpeed: 0,
            highlight: false,
            onError: "console"
        };
        board = Chessboard(boardElementId, config);
    }

    async function loadGame() {
        const path = window.location.pathname;
        const gameID = path.slice(path.lastIndexOf('/') + 1);

        const res = await fetch(`/game-info/${gameID}`);
        if (!res.ok) {
            throw new Error('Failed to fetch game info');
            showOverlay('The game requested to view is invalid.');
            return;
        }
        const gameData = await res.json();

        insertGameResultBlock(gameData.result, charToReason[gameData.reason], gameData.white, gameData.black);


        // Display player names
        const whiteNameElem = document.getElementById('bottom-name');
        const blackNameElem = document.getElementById('top-name');
        whiteNameElem.innerHTML = `<a href="/player/${gameData.white}" class="text-blue-600 hover:underline">${gameData.white}</a>`;
        blackNameElem.innerHTML = `<a href="/player/${gameData.black}" class="text-blue-600 hover:underline">${gameData.black}</a>`;

        // Apply moves
        if(gameData.moves.length) {
            const moves = gameData.moves.split(' ');
            moves.forEach((move, idx) => {
                makeMove(move, false);
                updateMoveHistory(move, idx%2 == 0 ? "w" : "b", board.fen(), idx);
            });
        }

        const stamps = gameData.timeStamps.split(' ');
        stamps.forEach((stamp, idx) => {
            timeStamps.push([parseInt(stamp.substring(0, stamp.indexOf('-') + 1)), parseInt(stamp.substring(stamp.indexOf('-') + 1, stamp.length))]);
        });

        currentMoveIndex = 0;

        board.position(moveHistory[0], false);
        updateTimersAt(0);
        updateNavButtons();
    }

    initBoard();
    setupEventListeners();
    loadGame();
    getUserStatus();

// });
