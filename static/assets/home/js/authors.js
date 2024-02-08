const authorsToggle = document.getElementById('authors-toggle');
        const authorsContainer = document.getElementById('authors-container');
    
        authorsToggle.addEventListener('click', () => {
            if (authorsContainer.classList.contains('hidden')) {
                authorsContainer.classList.remove('hidden');
                authorsToggle.querySelector('svg').classList.add('rotate-180');
            } else {
                authorsContainer.classList.add('hidden');
                authorsToggle.querySelector('svg').classList.remove('rotate-180');
            }
        });