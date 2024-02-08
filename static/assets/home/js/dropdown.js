const dropdownButton = document.getElementById('dropdownHoverButton');
const dropdownMenu = document.getElementById('dropdownHover');

let isDropdownVisible = false;

dropdownButton.addEventListener('mouseenter', () => {
  dropdownMenu.classList.add('dropdown-enter');
  dropdownMenu.classList.remove('hidden');
  setTimeout(() => {
    dropdownMenu.classList.add('dropdown-enter-active');
    isDropdownVisible = true;
  }, 0);
});

dropdownButton.addEventListener('mouseleave', () => {
  setTimeout(() => {
    if (!isDropdownVisible) {
      dropdownMenu.classList.remove('dropdown-enter-active');
      dropdownMenu.classList.add('dropdown-leave');
      setTimeout(() => {
        dropdownMenu.classList.remove('dropdown-enter', 'dropdown-leave');
        dropdownMenu.classList.add('hidden');
      }, 300);
    }
  }, 300);
});

dropdownMenu.addEventListener('mouseenter', () => {
  isDropdownVisible = true;
});

dropdownMenu.addEventListener('mouseleave', () => {
  isDropdownVisible = false;
  setTimeout(() => {
    if (!isDropdownVisible) {
      dropdownMenu.classList.remove('dropdown-enter-active');
      dropdownMenu.classList.add('dropdown-leave');
      setTimeout(() => {
        dropdownMenu.classList.remove('dropdown-enter', 'dropdown-leave');
        dropdownMenu.classList.add('hidden');
      }, 300);
    }
  }, 300);
});
